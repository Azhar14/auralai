// Copyright (c) 2023 by Rockchip Electronics Co., Ltd. All Rights Reserved.
// Stabilized Camera & DFPlayer Integration

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
#include <termios.h>

#include "opencv2/core/core.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/videoio.hpp"

#include "dma_alloc.cpp"
#define USE_DMA 0

// ========================================================
// FUNGSI DFPLAYER MINI UART
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
    printf("[INFO] DFPlayer UART Berhasil Dibuka di %s!\n", port);
    return fd;
}

void play_track(int fd, uint8_t track_num) {
    if (fd == -1) return;
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
    // Matikan RKIPC agar kamera bebas
    system("killall rkipc 2>/dev/null");
    usleep(500000); // Tunggu setengah detik agar kamera benar-benar rilis

    const char *model_path = argv[1];

    clock_t start_time;
    clock_t end_time;
    char text[64];
    float fps = 0;

    int model_width  = 640;
    int model_height = 640;
    int channels = 3;

    rknn_app_context_t rknn_app_ctx;
    object_detect_result_list od_results;
    memset(&rknn_app_ctx, 0, sizeof(rknn_app_context_t));

    init_yolov5_model(model_path, &rknn_app_ctx);
    init_post_process();

    int uart_fd = init_uart("/dev/ttyS3"); 
    time_t last_play_time = 0; 

    // Setup Tampilan Sementara (Tidak wajib dipakai jika tanpa LCD)
    int disp_width = 640;
    int disp_height = 480;

    // Inisialisasi Kamera dengan V4L2 Native (PENTING untuk menghindari SegFault)
    cv::VideoCapture cap;
    // Gunakan argumen API preference CAP_V4L2 secara eksplisit
    cap.open(0, cv::CAP_V4L2);
    
    if(!cap.isOpened()){
        printf("[ERROR] Gagal membuka kamera OpenCV!\n");
        return -1;
    }
    
    // Set resolusi standar setelah cap terbuka
    cap.set(cv::CAP_PROP_FRAME_WIDTH, disp_width);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, disp_height);

    cv::Mat bgr;
    cv::Mat bgr_model_input(model_height, model_width, CV_8UC3, rknn_app_ctx.input_mems[0]->virt_addr);

    printf("[INFO] Mulai Proses Deteksi AI...\n");

    while(1)
    {
        start_time = clock();
        
        // Ambil frame (Perbaikan potensi SegFault)
        cap.read(bgr);
        if (bgr.empty()) {
            printf("Frame kosong, mengulang tangkapan...\n");
            usleep(10000);
            continue;
        }

        // Resize ke 640x640 untuk model YOLO
        cv::resize(bgr, bgr_model_input, cv::Size(model_width,model_height), 0, 0, cv::INTER_LINEAR);
        inference_yolov5_model(&rknn_app_ctx, &od_results);

        for (int i = 0; i < od_results.count; i++)
        {
            object_detect_result *det_result = &(od_results.results[i]);
            mapCoordinates(bgr, bgr_model_input, &det_result->box.left,  &det_result->box.top);
            mapCoordinates(bgr, bgr_model_input, &det_result->box.right, &det_result->box.bottom);

            // Log ke terminal
            printf("Deteksi ID: %d | Conf: %.1f%%\n", det_result->cls_id, det_result->prop * 100);
        }

        // ========================================================
        // TRIGGER AUDIO DFPLAYER MINI
        // ========================================================
        if (od_results.count > 0) {
            time_t current_time = time(NULL);
            if (difftime(current_time, last_play_time) >= 3.0) {
                int cls_id = od_results.results[0].cls_id; 
                uint8_t track_id = cls_id + 1; 
                printf("[AUDIO] MP3 Track %04d\n", track_id);
                play_track(uart_fd, track_id);
                last_play_time = current_time; 
            }
        }

        end_time = clock();
        fps= (float) (CLOCKS_PER_SEC / (end_time - start_time)) ;
    }
    
    deinit_post_process();
    if (uart_fd != -1) close(uart_fd);
    release_yolov5_model(&rknn_app_ctx);
    return 0;
}