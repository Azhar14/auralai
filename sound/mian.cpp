// Copyright (c) 2023 by Rockchip Electronics Co., Ltd. All Rights Reserved.
// Modifikasi oleh AI untuk mendeteksi uang & memutar audio via DFPlayer

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yolov5.h"
#include "image_utils.h"
#include "file_utils.h"
#include "image_drawing.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <time.h>
#include <termios.h> // Library untuk UART DFPlayer

//opencv
#include "opencv2/core/core.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"

#include "dma_alloc.cpp"

#define USE_DMA 0

// ========================================================
// [TAMBAHAN] FUNGSI DFPLAYER MINI UART
// ========================================================
int init_uart(const char* port) {
    int fd = open(port, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd == -1) {
        printf("[ERROR] Gagal membuka port Serial %s\n", port);
        return -1;
    }
    struct termios options;
    tcgetattr(fd, &options);
    options.c_cflag = B9600 | CS8 | CLOCAL | CREAD;
    options.c_iflag = IGNPAR;
    options.c_oflag = 0;
    options.c_lflag = 0;
    tcflush(fd, TCIFLUSH);
    tcsetattr(fd, TCSANOW, &options);
    printf("[INFO] DFPlayer UART3 Berhasil Dibuka di %s!\n", port);
    return fd;
}

void play_track(int fd, uint8_t track_num) {
    if (fd == -1) return;
    // Format Command DFPlayer: 7E FF 06 03 00 00 [Track] [ChecksumHigh] [ChecksumLow] EF
    uint8_t cmd[10] = {0x7E, 0xFF, 0x06, 0x03, 0x00, 0x00, track_num, 0x00, 0x00, 0xEF};
    uint16_t checksum = 0xFFFF - (0xFF + 0x06 + 0x03 + 0x00 + 0x00 + track_num) + 1;
    cmd[7] = (uint8_t)(checksum >> 8);
    cmd[8] = (uint8_t)checksum;
    write(fd, cmd, 10);
}
// ========================================================

void mapCoordinates(cv::Mat input, cv::Mat output, int *x, int *y) {
    float scaleX = (float)output.cols / (float)input.cols;
    float scaleY = (float)output.rows / (float)input.rows;

    *x = (int)((float)*x / scaleX);
    *y = (int)((float)*y / scaleY);
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        printf("%s <yolov5 model_path>\n", argv[0]);
        return -1;
    }
    system("RkLunch-stop.sh"); // Matikan program kamera default
    const char *model_path = argv[1];

    clock_t start_time;
    clock_t end_time;
    char text[32]; // Diperbesar dari 8 ke 32 agar teks tidak terpotong
    float fps = 0;

    int model_width    = 640;
    int model_height   = 640;
    int channels = 3;

    int ret;
    rknn_app_context_t rknn_app_ctx;
    object_detect_result_list od_results;
    memset(&rknn_app_ctx, 0, sizeof(rknn_app_context_t));

    init_yolov5_model(model_path, &rknn_app_ctx);
    init_post_process();

    // Init UART3 untuk DFPlayer (Luckfox Pico umumnya menggunakan ttyS3 atau ttyS4)
    // Sesuaikan jika DFPlayer dicolok ke pin TX/RX yang berbeda
    int uart_fd = init_uart("/dev/ttyS3"); 
    time_t last_play_time = 0; // Waktu putar audio terakhir

    //Init fb
    int disp_flag = 0;
    int pixel_size = 0;
    size_t screensize = 0;
    int disp_width = 0;
    int disp_height = 0;
    void* framebuffer = NULL;
    struct fb_fix_screeninfo fb_fix;
    struct fb_var_screeninfo fb_var;

    int framebuffer_fd = 0; 
    cv::Mat disp;

    int fb = open("/dev/fb0", O_RDWR);
    if(fb == -1)
        printf("Screen OFF!\n");
    else
        disp_flag = 1;

    if(disp_flag){
        ioctl(fb, FBIOGET_VSCREENINFO, &fb_var);
        ioctl(fb, FBIOGET_FSCREENINFO, &fb_fix);

        disp_width = fb_var.xres;
        disp_height = fb_var.yres;
        pixel_size = fb_var.bits_per_pixel / 8;

        screensize = disp_width * disp_height * pixel_size;
        framebuffer = (uint8_t*)mmap(NULL, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);

        if( pixel_size == 4 ) disp = cv::Mat(disp_height, disp_width, CV_8UC3);
        else if ( pixel_size == 2 ) disp = cv::Mat(disp_height, disp_width, CV_16UC1);

#if USE_DMA
        dma_buf_alloc(RV1106_CMA_HEAP_PATH, disp_width * disp_height * pixel_size, &framebuffer_fd, (void **) & (disp.data));
#endif
    }
    else{
        disp_height = 480;
        disp_width = 640;
    }

    //Init Opencv-mobile
    cv::VideoCapture cap;
    cv::Mat bgr(disp_height, disp_width, CV_8UC3);
    cv::Mat bgr_model_input(model_height, model_width, CV_8UC3, rknn_app_ctx.input_mems[0]->virt_addr);
    cap.set(cv::CAP_PROP_FRAME_WIDTH,  disp_width);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, disp_height);
    cap.open(0);

    while(1)
    {
        start_time = clock();
        cap >> bgr;

        cv::resize(bgr, bgr_model_input, cv::Size(model_width,model_height), 0, 0, cv::INTER_LINEAR);
        inference_yolov5_model(&rknn_app_ctx, &od_results);

        // Add rectangle and probability
        for (int i = 0; i < od_results.count; i++)
        {
            object_detect_result *det_result = &(od_results.results[i]);
            mapCoordinates(bgr, bgr_model_input, &det_result->box.left,  &det_result->box.top);
            mapCoordinates(bgr, bgr_model_input, &det_result->box.right, &det_result->box.bottom);

            cv::rectangle(bgr,cv::Point(det_result->box.left ,det_result->box.top),
                              cv::Point(det_result->box.right,det_result->box.bottom),cv::Scalar(0,255,0),3);

            sprintf(text, "ID:%d %.1f%%", det_result->cls_id, det_result->prop * 100);
            cv::putText(bgr,text,cv::Point(det_result->box.left, det_result->box.top - 8),
                                         cv::FONT_HERSHEY_SIMPLEX,0.5,
                                         cv::Scalar(0,255,0),2);
        }

        // ========================================================
        // [TAMBAHAN] TRIGGER AUDIO DFPLAYER MINI
        // ========================================================
        if (od_results.count > 0) {
            time_t current_time = time(NULL);
            
            // Cek apakah sudah lewat 3 detik dari suara terakhir (Cooldown anti-spam)
            if (difftime(current_time, last_play_time) >= 3.0) {
                
                // Ambil uang pertama yang terdeteksi di layar
                int cls_id = od_results.results[0].cls_id; 
                
                // Hitung Track MP3 (ID 0 jadi Track 1, dst)
                uint8_t track_id = cls_id + 1; 

                printf("[AUDIO] Dideteksi Uang ID:%d | Memutar MP3 Track %04d\n", cls_id, track_id);
                play_track(uart_fd, track_id);
                
                // Catat waktu sekarang agar delay 3 detik aktif kembali
                last_play_time = current_time; 
            }
        }
        // ========================================================

        if(disp_flag){
            sprintf(text,"fps=%.1f",fps);
            cv::putText(bgr,text,cv::Point(0, 20), cv::FONT_HERSHEY_SIMPLEX,0.5, cv::Scalar(0,255,0),1);

            if( pixel_size == 4 ) cv::cvtColor(bgr, disp, cv::COLOR_BGR2BGRA);
            else if( pixel_size == 2 ) cv::cvtColor(bgr, disp, cv::COLOR_BGR2BGR565);
            memcpy(framebuffer, disp.data, disp_width * disp_height * pixel_size);
#if USE_DMA
            dma_sync_cpu_to_device(framebuffer_fd);
#endif
        }
        
        end_time = clock();
        fps= (float) (CLOCKS_PER_SEC / (end_time - start_time)) ;
        memset(text,0,sizeof(text));
    }
    
    deinit_post_process();

    if(disp_flag){
        close(fb);
        munmap(framebuffer, screensize);
#if USE_DMA
        dma_buf_free(disp_width*disp_height*pixel_size, &framebuffer_fd, bgr.data);
#endif
    }

    if (uart_fd != -1) close(uart_fd);

    ret = release_yolov5_model(&rknn_app_ctx);
    if (ret != 0) printf("release_yolov5_model fail! ret=%d\n", ret);

    return 0;
}