/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * Copyright Kernel Labs Inc. 2017 <stoth@kernellabs.com>
 *
 * @file
 * video filter, negotiate yuv420p then analyze frame and attempt to extract a burnt in 32bit counter.
 *
 * usage:
 *  ffmpeg -y -i ../../LTN/20170329/cleanbars-and-counter.ts -vf klburnin -f null -
 */

#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "avfilter.h"
#include "internal.h"
#include "video.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"

typedef struct BurnContext
{
	const AVClass *class;
	uint64_t framecnt;
	uint64_t totalErrors;
	uint32_t framesProcessed;
	int inError;

	/* parameters */
	uint64_t line;
	uint64_t bitwidth;
	uint64_t bitheight;

} BurnContext;

#define OFFSET(x) offsetof(BurnContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption klburnin_options[] = {

	/* pixel row/line at which to the top of the digit box begins. */
	{ "line", "set line", OFFSET(line), AV_OPT_TYPE_INT, {.i64=200}, 1, 1080, FLAGS, "line" },

	/* With and height of each bit in pixels, usually digits are 30x30 pixels. */
	{ "bitwidth", "set bit width", OFFSET(bitwidth), AV_OPT_TYPE_INT, {.i64=30}, 1, 128, FLAGS, "bitwidth" },
	{ "bitheight", "set bit height", OFFSET(bitheight), AV_OPT_TYPE_INT, {.i64=30}, 1, 128, FLAGS, "bitheight" },

	{  NULL }
};

AVFILTER_DEFINE_CLASS(klburnin);

static int config_input(AVFilterLink *link)
{
	BurnContext *ctx = link->dst->priv;
	//const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(link->format);

	ctx->framecnt = 0;
	ctx->totalErrors = 0;
	ctx->framesProcessed = 0;
	ctx->inError = 1;

	return 0;
}

static int query_formats(AVFilterContext *ctx)
{
	AVFilterFormats *formats = NULL;
	int fmt;

	for (fmt = 0; av_pix_fmt_desc_get(fmt); fmt++) {
		//printf("fmt = %s\n", av_get_pix_fmt_name(fmt));
		//const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
		if (fmt != AV_PIX_FMT_YUV420P)
			continue;
		int ret;
#if 0
		if (desc->flags & AV_PIX_FMT_FLAG_HWACCEL)
			continue;
#endif
		if ((ret = ff_add_format(&formats, fmt)) < 0)
			return ret;
	}

	return ff_set_common_formats(ctx, formats);
}

static void analyzeFrame(BurnContext *ctx, AVFrame *frame, uint8_t *pic, uint32_t sizeBytes)
{       
        uint32_t bits = 0;
#if 0
	for (int i = 0; i < 10; i++)
		printf("%02x ", *(pic + i));
	printf("\n");
#endif

	/* Figure out where the vertical center of row of digits should be */
	int startline = ctx->line + (ctx->bitheight / 2);
	uint8_t *x = pic + (startline * frame->width);

	/* Decode 32 bits */
	for (int c = 31; c >= 0; c--) {
		x += (ctx->bitwidth / 2);
		if (*x > 0x80)
			bits |= (1 << c);
		x += (ctx->bitwidth / 2);
	}

        char t[160]; 
        time_t now = time(0);
        sprintf(t, "%s", ctime(&now));
        t[strlen(t) - 1] = 0;
        
        ctx->framesProcessed++;
#if 0
        /* Fake an error for test purposes. */
        if (ctx->framecnt == 58000)
                ctx->framecnt = 2;
#endif  
        if (ctx->framecnt && ctx->framecnt + 1 != bits) {
                ctx->totalErrors++;
                if (!ctx->inError)
                        fprintf(stderr, "\n%s: KL OSD counter discontinuity, expected %08" PRIx64 " got %08" PRIx32 "\n", t, ctx->framecnt + 1, bits);
                ctx->inError = 1;
        } else {
		if (ctx->inError)
			fprintf(stderr, "\n%s: KL OSD counter is incrementing, normal service resumes.\n", t);
		ctx->inError = 0;
	}
	ctx->framecnt = bits;
        
	printf("%s: Frame %dx%d fmt:%s buf:%p bytes:%d burned-in-frame#%08d totalframes#%08d totalErrors#%" PRIu64 "\n",
		t, frame->width, frame->height, av_get_pix_fmt_name(frame->format), pic, sizeBytes,
		bits, ctx->framesProcessed, ctx->totalErrors);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
	BurnContext *ctx = inlink->dst->priv;

	//printf("%s:%s(ctx=%p)\n", __FILE__, __func__, ctx);

	AVFilterLink *outlink = inlink->dst->outputs[0];
	AVFrame *out = ff_get_video_buffer(outlink, in->width, in->height);
	if (!out) {
		av_frame_free(&in);
		return AVERROR(ENOMEM);
	}

	av_frame_copy_props(out, in);
	av_frame_copy(out, in);

	analyzeFrame(ctx, out, out->data[0], out->width * out->height);

	av_frame_free(&in);
	return ff_filter_frame(outlink, out);
}

static const AVFilterPad avfilter_vf_klburnin_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
	.config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_klburnin_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_klburnin = {
    .name        = "klburnin",
    .description = NULL_IF_CONFIG_SMALL("Copy the input video, burn in a 32bit counter and output."),
    .priv_size   = sizeof(BurnContext),
    .priv_class  = &klburnin_class,
    .inputs      = avfilter_vf_klburnin_inputs,
    .outputs     = avfilter_vf_klburnin_outputs,
    .query_formats = query_formats,
};
