#include <stdint.h>
#include <iostream>
#include <cstring>
#include <fstream>
#include <chrono>
#include <thread>
#include <math.h>
#include <cstdio>
#include "headers/MLX90640_API.h"
#include "lib/fb.h"

#define MLX_I2C_ADDR 0x33

#define IMAGE_SCALE 4

// Valid frame rates are 1, 2, 4, 8, 16, 32 and 64
// The i2c baudrate is set to 1mhz to support these
#define FPS 8
#define FRAME_TIME_MICROS (1000000/FPS)

// Despite the framerate being ostensibly FPS hz
// The frame is often not ready in time
// This offset is added to the FRAME_TIME_MICROS
// to account for this.
#define OFFSET_MICROS 850

void put_pixel_false_colour(int x, int y, double v) {
    // Heatmap code borrowed from: http://www.andrewnoske.com/wiki/Code_-_heatmaps_and_color_gradients
    const int NUM_COLORS = 7;
    static float color[NUM_COLORS][3] = { {0,0,0}, {0,0,1}, {0,1,0}, {1,1,0}, {1,0,0}, {1,0,1}, {1,1,1} };
    int idx1, idx2;
    float fractBetween = 0;
    float vmin = 5.0;
    float vmax = 50.0;
    float vrange = vmax-vmin;
    v -= vmin;
    v /= vrange;
    if(v <= 0) {idx1=idx2=0;}
    else if(v >= 1) {idx1=idx2=NUM_COLORS-1;}
    else
    {
        v *= (NUM_COLORS-1);
        idx1 = floor(v);
        idx2 = idx1+1;
        fractBetween = v - float(idx1);
    }

    int ir, ig, ib;


    ir = (int)((((color[idx2][0] - color[idx1][0]) * fractBetween) + color[idx1][0]) * 255.0);
    ig = (int)((((color[idx2][1] - color[idx1][1]) * fractBetween) + color[idx1][1]) * 255.0);
    ib = (int)((((color[idx2][2] - color[idx1][2]) * fractBetween) + color[idx1][2]) * 255.0);

//    for(int px = 0; px < IMAGE_SCALE; px++){
//        for(int py = 0; py < IMAGE_SCALE; py++){
//            fb_put_pixel(x + px, y + py, ir, ig, ib);
//        }
//    }
    fb_put_pixel(x, y, ir, ig, ib);
}


void draw_char(int x, int y, char c, int scale,
               int r, int g, int b) {
    static const uint8_t digits[10][7] = {
        {14,17,19,21,25,17,14},
        {4,12,4,4,4,4,14},
        {14,17,1,2,4,8,31},
        {30,1,1,14,1,1,30},
        {2,6,10,18,31,2,2},
        {31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14},
        {31,1,2,4,8,8,8},
        {14,17,17,14,17,17,14},
        {14,17,17,15,1,1,14}
    };

    static const uint8_t letterC[7] = {14,17,16,16,16,17,14};
    static const uint8_t dot[7] = {0,0,0,0,0,4,4};
    static const uint8_t blank[7] = {0,0,0,0,0,0,0};

    const uint8_t* glyph = blank;

    if (c >= '0' && c <= '9')
        glyph = digits[c - '0'];
    else if (c == 'C' || c == 'c')
        glyph = letterC;
    else if (c == '.')
        glyph = dot;

    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 5; col++) {
            if (glyph[row] & (1 << (4 - col))) {
                for (int dy = 0; dy < scale; dy++) {
                    for (int dx = 0; dx < scale; dx++) {
                        fb_put_pixel(x + col * scale + dx,
                                     y + row * scale + dy,
                                     r, g, b);
                    }
                }
            }
        }
    }
}

void draw_text(int x, int y, const char* text, int scale,
               int r, int g, int b) {
    while (*text) {
        draw_char(x, y, *text, scale, r, g, b);
        x += 6 * scale;
        text++;
    }
}

void draw_crosshair(int cx, int cy) {
    // Cerny krizek, tloustka 3 pixely
    for (int d = -12; d <= 12; d++) {
        for (int t = -1; t <= 1; t++) {
            fb_put_pixel(cx + d, cy + t, 0, 0, 0);
            fb_put_pixel(cx + t, cy + d, 0, 0, 0);
        }
    }
}

int main(){
    static uint16_t eeMLX90640[832];
    float emissivity = 1;
    uint16_t frame[834];
    static float mlx90640To[768];
    float eTa;

    auto frame_time = std::chrono::microseconds(FRAME_TIME_MICROS + OFFSET_MICROS);

    MLX90640_SetDeviceMode(MLX_I2C_ADDR, 0);
    MLX90640_SetSubPageRepeat(MLX_I2C_ADDR, 0);
    switch(FPS){
        case 1:
            MLX90640_SetRefreshRate(MLX_I2C_ADDR, 0b001);
            break;
        case 2:
            MLX90640_SetRefreshRate(MLX_I2C_ADDR, 0b010);
            break;
        case 4:
            MLX90640_SetRefreshRate(MLX_I2C_ADDR, 0b011);
            break;
        case 8:
            MLX90640_SetRefreshRate(MLX_I2C_ADDR, 0b100);
            break;
        case 16:
            MLX90640_SetRefreshRate(MLX_I2C_ADDR, 0b101);
            break;
        case 32:
            MLX90640_SetRefreshRate(MLX_I2C_ADDR, 0b110);
            break;
        case 64:
            MLX90640_SetRefreshRate(MLX_I2C_ADDR, 0b111);
            break;
        default:
            printf("Unsupported framerate: %d", FPS);
            return 1;
    }
    MLX90640_SetChessMode(MLX_I2C_ADDR);

    paramsMLX90640 mlx90640;
    MLX90640_DumpEE(MLX_I2C_ADDR, eeMLX90640);
    MLX90640_ExtractParameters(eeMLX90640, &mlx90640);

    fb_init();
    draw_crosshair(120, 120);

    while (1) {
    // Nacteni a vykresleni termogramu


        auto start = std::chrono::system_clock::now();
        auto error = MLX90640_GetFrameData(MLX_I2C_ADDR, frame);
        if (error < 0) {
	    printf("Failed to get frame data.\n");
	    exit(1);
	}
        // MLX90640_InterpolateOutliers(frame, eeMLX90640);

        eTa = MLX90640_GetTa(frame, &mlx90640);
        MLX90640_CalculateTo(frame, &mlx90640, emissivity, eTa, mlx90640To);

        MLX90640_BadPixelsCorrection((&mlx90640)->brokenPixels, mlx90640To, 1, &mlx90640);
        MLX90640_BadPixelsCorrection((&mlx90640)->outlierPixels, mlx90640To, 1, &mlx90640);

//        for(int y = 0; y < 24; y++){
//            for(int x = 0; x < 32; x++){
//                float val = mlx90640To[32 * (23-y) + x];
//                put_pixel_false_colour((y*IMAGE_SCALE), (x*IMAGE_SCALE), val);
//            }
//        }

        for (int y = 0; y < 24; y++) {
            for (int x = 0; x < 32; x++) {
                float val = mlx90640To[32 * (23-y) + x];

//        int x0 = x * 7;
//        int x1 = (x + 1) * 7;
//        int y0 = y * 10;
//        int y1 = (y + 1) * 10;

                int x0 = x * 240 / 32;
                int x1 = (x + 1) * 240 / 32;
                int y0 = y * 10;
                int y1 = (y + 1) * 10;

                for (int py = y0; py < y1; py++) {
                    for (int px = x0; px < x1; px++) {

// Nevykreslovat pres ramecek s teplotou

                        if (px >= 2 && px < 90 && py >= 218 && py < 239) {
                            continue;
                        }


// Nevykreslovat pres oblast krizku
                        if ((px >= 108 && px < 133 && py >= 119 && py < 122) || (px >= 119 && px < 122 && py >= 108 && py < 133)) {
                            continue;
                        }

                        put_pixel_false_colour(px, py, val);
                    }
                }
            }
        }

 // Krizek uprostred displeje
//        draw_crosshair(120, 120);

        // Teplota senzoru v bode (16, 12)
        float centerTemp = mlx90640To[32 * 12 + 16];

        char tempText[20];
        snprintf(tempText, sizeof(tempText), "%.1f C", centerTemp);

        // Tmavy podklad v levem dolnim rohu
        for (int py = 218; py < 239; py++) {
            for (int px = 2; px < 90; px++) {
                fb_put_pixel(px, py, 0, 0, 0);
            }
        }

        // Bila teplota
        draw_text(6, 222, tempText, 2, 255, 255, 255);


        auto end = std::chrono::system_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        std::this_thread::sleep_for(std::chrono::microseconds(frame_time - elapsed));
    }

    fb_cleanup();
    return 0;
}
