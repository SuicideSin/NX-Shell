#include <stdio.h>
#include <stdbool.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include "SDL_helper.h"
#include "log.h"

#define SDL_USEREVENT_REFRESH  (SDL_USEREVENT + 1)

static bool s_playing_exit = false;
static bool s_playing_pause = false;
static SDL_Renderer *sdl_renderer;
static SDL_Texture *sdl_texture;

int sdl_thread_handle_refreshing(void *opaque) {
	SDL_Event sdl_event;

	while (!s_playing_exit) {
		if (!s_playing_pause) {
			sdl_event.type = SDL_USEREVENT_REFRESH;
			SDL_PushEvent(&sdl_event);
		}

		SDL_Delay(40);
	}

	return 0;
}

static int render_frames(AVPacket *p_packet, AVCodecContext *p_codec_ctx, AVFrame *p_frame) {
	sdl_texture = SDL_CreateTexture(sdl_renderer,  SDL_PIXELFORMAT_IYUV,  SDL_TEXTUREACCESS_STREAMING, p_codec_ctx->width, p_codec_ctx->height);

	int ret = avcodec_send_packet(p_codec_ctx, p_packet);
	if (ret < 0)
		return ret;

	while (ret >= 0) {
		ret = avcodec_receive_frame(p_codec_ctx, p_frame);

		 if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
		 	break;
		 else if (ret < 0)
		 	return ret;

		 if (ret >= 0) {
		 	SDL_UpdateYUVTexture(sdl_texture, NULL, p_frame->data[0], p_frame->linesize[0], p_frame->data[1], p_frame->linesize[1], p_frame->data[2], p_frame->linesize[2]); 
		 	SDL_RenderClear(sdl_renderer);
			SDL_RenderCopy(sdl_renderer, sdl_texture, NULL,  NULL); 
			SDL_RenderPresent(sdl_renderer);
			SDL_DestroyTexture(sdl_texture);
		 }

		 av_frame_unref(p_frame);
	}

	return 0;
}

int Menu_PlayVideo(char *path) {
	int v_idx = -1, a_idx = -1, ret = 0, res = 0;
	AVFormatContext *p_fmt_ctx = NULL;
	AVCodecContext *p_codec_ctx = NULL;
	AVCodecParameters *p_codec_par = NULL;
	AVCodec *p_codec = NULL;
	AVFrame *p_frame = NULL;
	AVPacket *p_packet = NULL;
	SDL_Window *screen;
	SDL_Thread *sdl_thread;
	SDL_Event sdl_event;

	if ((ret = avformat_open_input(&p_fmt_ctx, path, NULL, NULL)) != 0) {
		DEBUG_LOG("avformat_open_input() failed %d\n", ret);
		res = -1;
	}

	if ((ret = avformat_find_stream_info(p_fmt_ctx, NULL)) < 0) {
		DEBUG_LOG("avformat_find_stream_info() failed %d\n", ret);
		res = -1;
	}

	for (int i = 0; i < p_fmt_ctx->nb_streams; i++) {
		AVCodecParameters *pLocalCodecParameters =  NULL;
		pLocalCodecParameters = p_fmt_ctx->streams[i]->codecpar;
		AVCodec *pLocalCodec = NULL;
		pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);

		if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO) {
			v_idx = i;
			p_codec = pLocalCodec;
			p_codec_par = pLocalCodecParameters;
			DEBUG_LOG("Video stream, index %d\n", v_idx);
		}
		if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO)
			a_idx = i;
	}
	if (a_idx == -1) {
		DEBUG_LOG("Cann't find a video stream\n");
		res = -1;
	}

	p_codec_ctx = avcodec_alloc_context3(p_codec);
	if (!p_codec_ctx)
		res = -1;

	if ((ret = avcodec_parameters_to_context(p_codec_ctx, p_codec_par)) < 0) {
		DEBUG_LOG("avcodec_parameters_to_context() failed %d\n", ret);
		res = -1;
	}

	if ((ret = avcodec_open2(p_codec_ctx, p_codec, NULL)) < 0) {
		DEBUG_LOG("avcodec_open2() failed %d\n", ret);
		res = -1;
	}

	p_frame = av_frame_alloc();
	if (!p_frame) {
		DEBUG_LOG("av_frame_alloc() for p_frame failed\n");
		res = -1;
	}

	p_packet = av_packet_alloc();
	if (!p_packet) {
		DEBUG_LOG("av_packet_alloc() for p_packet failed\n");
		res = -1;
	}

	screen = SDL_CreateWindow("NX Shell - Video Player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, p_codec_ctx->width, p_codec_ctx->height, SDL_WINDOW_FULLSCREEN);

	if (screen == NULL) {
		DEBUG_LOG("SDL_CreateWindow() failed: %s\n", SDL_GetError());  
		res = -1;
	}
	sdl_renderer = SDL_CreateRenderer(screen, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (sdl_renderer == NULL) {
		DEBUG_LOG("SDL_CreateRenderer() failed: %s\n", SDL_GetError());  
		res = -1;
	}

	sdl_thread = SDL_CreateThread(sdl_thread_handle_refreshing, NULL, NULL);
	if (sdl_thread == NULL) {
		DEBUG_LOG("SDL_CreateThread() failed: %s\n", SDL_GetError());
		res = -1;
	}

	while (appletMainLoop()) {
		SDL_WaitEvent(&sdl_event);

		if (sdl_event.type == SDL_USEREVENT_REFRESH) {
			while (av_read_frame(p_fmt_ctx, p_packet) == 0) {
				if (p_packet->stream_index == v_idx) {
					ret = render_frames(p_packet, p_codec_ctx, p_frame);

					if (ret < 0)
						break;
				}

				av_packet_unref(p_packet);
			}

			avformat_close_input(&p_fmt_ctx);
			avformat_free_context(p_fmt_ctx);
			av_packet_free(&p_packet);
			av_frame_free(&p_frame);
			avcodec_free_context(&p_codec_ctx);
		}

		hidScanInput();
		u32 kDown = hidKeysDown(CONTROLLER_P1_AUTO);
		if (kDown & KEY_B)
			break;

		if (kDown & KEY_A) {
			s_playing_pause = !s_playing_pause;
			DEBUG_LOG("player %s\n", s_playing_pause ? "pause" : "continue");
		}
	}

	SDL_DestroyTexture(sdl_texture);
    SDL_DestroyRenderer(sdl_renderer);
	SDL_DestroyWindow(screen);
	avformat_close_input(&p_fmt_ctx);
    avformat_free_context(p_fmt_ctx);
    av_packet_free(&p_packet);
	av_frame_free(&p_frame);
	avcodec_free_context(&p_codec_ctx);
    return res;
}
