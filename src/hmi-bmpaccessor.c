/***************************************************************************
 *
 * Copyright 2010,2011 BMW Car IT GmbH
 * Copyright (C) 2012 DENSO CORPORATION and Robert Bosch Car Multimedia Gmbh
 *
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include "hmi-bmpaccessor.h"

typedef struct _BITMAPFILEHEADER {
    unsigned short bfType;
    unsigned int   bfSize;
    unsigned short bfReserved1;
    unsigned short bfReserved2;
    unsigned int   bfOffBits;
} BITMAPFILEHEADER;

typedef struct _BITMAPINFOHEADER {
    unsigned int   biSize;
    int            biWidth;
    int            biHeight;
    unsigned short biPlanes;
    unsigned short biBitCount;
    unsigned int   biCompression;
    unsigned int   biSizeImage;
    int            biXPixPerMeter;
    int            biYPixPerMeter;
    unsigned int   biClrUsed;
    unsigned int   biClrImporant;
} BITMAPINFOHEADER;

/**
 *  \brief Open windows bitmap file
 *  \param[in] bitmap file path
 *  \return context of bmpaccessor
*/
ctx_bmpaccessor* bmpaccessor_open(const char* path)
{
    FILE* fp;
    ctx_bmpaccessor* ctx = NULL;
    BITMAPFILEHEADER file_head;
    BITMAPINFOHEADER info_head;
    size_t read_num = 0;
    unsigned char* p_in = NULL;
    unsigned char* p_in_pnt = NULL;
    unsigned char* p_out_pnt = NULL;
    int cntX = 0;
    int cntY = 0;

    do
    {
        fp = fopen(path, "rb");
        if (fp == NULL)
        {
            printf("failed to open %s\n", path);
            break;
        }

        ctx = (ctx_bmpaccessor*)malloc(sizeof(ctx_bmpaccessor));
        if (NULL == ctx)
        {
            printf("failed to allocate context\n");
            break;
        }

        read_num = fread(&file_head.bfType, sizeof(unsigned short), 1, fp);
        if (read_num != 1)
        {
            printf("failed to read bfType\n");
            break;
        }
        read_num = fread(&file_head.bfSize, sizeof(unsigned int), 2, fp);
        if (read_num != 2)
        {
            printf("failed to read bfSize\n");
            break;
        }
        read_num = fread(&file_head.bfOffBits, sizeof(unsigned int), 1, fp);
        if (read_num != 1)
        {
            printf("failed to read bfOffBits\n");
            break;
        }
        printf("bfSize   : %d(0x%x)\n", file_head.bfSize,    file_head.bfSize);
        printf("bfOffBits: %d(0x%x)\n", file_head.bfOffBits, file_head.bfOffBits);

        read_num = fread(&info_head.biSize, sizeof(unsigned int), 1, fp);
        if (read_num != 1)
        {
            printf("failed to read biSize\n");
            break;
        }
        read_num = fread(&info_head.biWidth, sizeof(unsigned int), 2, fp);
        if (read_num != 2)
        {
            printf("failed to read image size\n");
            break;
        }
        read_num = fread(&info_head.biPlanes, sizeof(unsigned short), 2, fp);
        if (read_num != 2)
        {
            printf("failed to read biBitCount\n");
            break;
        }

        printf("biSize   : %d(0x%x)\n", info_head.biSize,     info_head.biSize);
        printf("biWidth  : %d(0x%x)\n", info_head.biWidth,    info_head.biWidth);
        printf("biHeight : %d(0x%x)\n", info_head.biHeight,   info_head.biHeight);
        printf("bitCount : %d(0x%x)\n", info_head.biBitCount, info_head.biBitCount);

        ctx->bitCountOrg = (info_head.biBitCount>>3);
        ctx->bitCount = 4;
        if (0 >= ctx->bitCountOrg)
        {
            printf("support bitCounts is only 24BPP with non-compression\n");
            break;
        }
        ctx->width  = info_head.biWidth;
        if (0 > info_head.biHeight)
        {
            ctx->height = -info_head.biHeight;
        }
        else
        {
            ctx->height = info_head.biHeight;
        }
        ctx->strideOrg = ctx->width * ctx->bitCountOrg;
        ctx->stride = (ctx->width<<2);
        p_in = (void*)malloc(ctx->strideOrg * ctx->height);
        if (NULL == p_in)
        {
            printf("failed to allocate work memory\n");
            break;
        }
        ctx->data = (void*)malloc(ctx->stride * ctx->height);
        if (NULL == ctx->data)
        {
            printf("failed to allocate image memory\n");
            break;
        }
        fseek(fp, file_head.bfOffBits, SEEK_SET);
        read_num = fread(p_in, ctx->strideOrg * ctx->height, 1, fp);
        if (read_num != 1)
        {
            printf("failed to read header\n");
            break;
        }
        p_in_pnt = p_in;
        p_out_pnt = ctx->data;
        for (cntY = 0; cntY < ctx->height; cntY++)
        {
            p_out_pnt = (unsigned char*)ctx->data + (ctx->height - cntY - 1) * ctx->stride;
            for (cntX = 0; cntX < ctx->width; cntX++)
            {
                *(p_out_pnt + 0) = *(p_in_pnt + 0);
                *(p_out_pnt + 1) = *(p_in_pnt + 1);
                *(p_out_pnt + 2) = *(p_in_pnt + 2);
                //*(p_out_pnt + 3) = 0;
                *(p_out_pnt + 3) = 0xFF;
                p_out_pnt += 4;
                p_in_pnt += 3;
            }
        }

        free(p_in);
        fclose(fp);

        printf("%s is %d x %d\n", path, ctx->width, ctx->height);

        return ctx;

    } while(0);

    if (NULL != ctx)
    {
        free(ctx);
    }
    if (NULL != p_in)
    {
        free(p_in);
    }
    if (NULL != fp)
    {
        fclose(fp);
    }

    return NULL;
}

/**
 *  \brief Close windows bitmap file
 *  \param[in] context of bmpaccessor_open
*/
void bmpaccessor_close(ctx_bmpaccessor* ctx)
{
    if (NULL == ctx)
    {
         return;
    }
    if (NULL != ctx->data)
    {
        free(ctx->data);
    }
    free(ctx);
}

/* End of File */
