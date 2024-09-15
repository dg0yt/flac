/* libFLAC - Free Lossless Audio Codec
 * Copyright (C) 2002-2009  Josh Coalson
 * Copyright (C) 2011-2024  Xiph.Org Foundation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * - Neither the name of the Xiph.org Foundation nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h> /* for memcpy() */
#include "FLAC/assert.h"
#include "share/alloc.h" /* for free() */
#include "private/ogg_decoder_aspect.h"
#include "private/ogg_mapping.h"
#include "private/macros.h"

/***********************************************************************
 *
 * Public class methods
 *
 ***********************************************************************/

FLAC__bool FLAC__ogg_decoder_aspect_init(FLAC__OggDecoderAspect *aspect)
{
	/* we will determine the serial number later if necessary */
	if(ogg_stream_init(&aspect->stream_state, aspect->serial_number) != 0)
		return false;

	if(ogg_sync_init(&aspect->sync_state) != 0)
		return false;

	aspect->version_major = ~(0u);
	aspect->version_minor = ~(0u);

	aspect->need_serial_number = aspect->use_first_serial_number || aspect->decode_chained_stream;

	aspect->end_of_stream = false;
	aspect->have_working_page = false;
	aspect->end_of_link = false;

	aspect->current_linknumber = 0;
	aspect->number_of_links_indexed = 0;
	aspect->number_of_links_detected = 0;

	if(NULL == (aspect->linkdetails = safe_realloc_mul_2op_(NULL,4,sizeof(FLAC__OggDecoderAspect_LinkDetails))))
		return false;
	memset(aspect->linkdetails, 0, 4 * sizeof(FLAC__OggDecoderAspect_LinkDetails));

	return true;
}

void FLAC__ogg_decoder_aspect_finish(FLAC__OggDecoderAspect *aspect)
{
	(void)ogg_sync_clear(&aspect->sync_state);
	(void)ogg_stream_clear(&aspect->stream_state);
	free(aspect->linkdetails);
}

void FLAC__ogg_decoder_aspect_set_serial_number(FLAC__OggDecoderAspect *aspect, long value)
{
	aspect->use_first_serial_number = false;
	aspect->serial_number = value;
}

void FLAC__ogg_decoder_aspect_set_defaults(FLAC__OggDecoderAspect *aspect)
{
	aspect->use_first_serial_number = true;
	aspect->decode_chained_stream = false;
}

void FLAC__ogg_decoder_aspect_flush(FLAC__OggDecoderAspect *aspect)
{
	(void)ogg_stream_reset(&aspect->stream_state);
	(void)ogg_sync_reset(&aspect->sync_state);
	aspect->end_of_stream = false;
	aspect->have_working_page = false;
	aspect->end_of_link = false;
}

void FLAC__ogg_decoder_aspect_reset(FLAC__OggDecoderAspect *aspect)
{
	FLAC__ogg_decoder_aspect_flush(aspect);
	aspect->current_linknumber = 0;

	if(aspect->use_first_serial_number || aspect->decode_chained_stream)
		aspect->need_serial_number = true;
}

void FLAC__ogg_decoder_aspect_next_link(FLAC__OggDecoderAspect* aspect)
{
	aspect->end_of_link = false;
	aspect->current_linknumber++;
}

void FLAC__ogg_decoder_aspect_set_decode_chained_stream(FLAC__OggDecoderAspect* aspect, FLAC__bool value)
{
	aspect->decode_chained_stream = value;
}

FLAC__bool FLAC__ogg_decoder_aspect_get_decode_chained_stream(FLAC__OggDecoderAspect* aspect)
{
	return aspect->decode_chained_stream;
}

FLAC__OggDecoderAspect_TargetLink * FLAC__ogg_decoder_aspect_get_target_link(FLAC__OggDecoderAspect* aspect, FLAC__uint64 target_sample)
{
	/* This returns the link containing the seek target if known. In
	 * effect, this function always returns NULL if no links have been
	 * indexed */

	uint32_t current_link = 0;
	uint32_t total_samples = 0;

	while(current_link < aspect->number_of_links_indexed) {
		total_samples += aspect->linkdetails[current_link].samples;
		if(target_sample < total_samples) {
			aspect->target_link.serial_number = aspect->linkdetails[current_link].serial_number;
			aspect->target_link.start_byte = aspect->linkdetails[current_link].start_byte;
			aspect->target_link.samples_in_preceding_links = total_samples - aspect->linkdetails[current_link].samples;
			aspect->target_link.end_byte = aspect->linkdetails[current_link].end_byte;
			aspect->target_link.samples_this_link = aspect->linkdetails[current_link].samples;
			aspect->target_link.linknumber = current_link;
			return &aspect->target_link;
		}
		current_link++;
	}
	return NULL;
}

void FLAC__ogg_decoder_aspect_set_seek_parameters(FLAC__OggDecoderAspect *aspect, FLAC__OggDecoderAspect_TargetLink *target_link)
{
	if(target_link == 0) {
		aspect->is_seeking = false;
	}
	else {
		aspect->need_serial_number = false;
		aspect->current_linknumber = target_link->linknumber;
		aspect->serial_number = target_link->serial_number;
		ogg_stream_reset_serialno(&aspect->stream_state, aspect->serial_number);
		aspect->is_seeking = true;
	}
}


FLAC__OggDecoderAspectReadStatus FLAC__ogg_decoder_aspect_read_callback_wrapper(FLAC__OggDecoderAspect *aspect, FLAC__byte buffer[], size_t *bytes, FLAC__OggDecoderAspectReadCallbackProxy read_callback, FLAC__StreamDecoderTellCallback tell_callback, const FLAC__StreamDecoder *decoder, void *client_data)
{
	static const size_t OGG_BYTES_CHUNK = 8192;
	const size_t bytes_requested = *bytes;

	const uint32_t header_length =
		FLAC__OGG_MAPPING_PACKET_TYPE_LENGTH +
		FLAC__OGG_MAPPING_MAGIC_LENGTH +
		FLAC__OGG_MAPPING_VERSION_MAJOR_LENGTH +
		FLAC__OGG_MAPPING_VERSION_MINOR_LENGTH +
		FLAC__OGG_MAPPING_NUM_HEADERS_LENGTH;

	/*
	 * The FLAC decoding API uses pull-based reads, whereas Ogg decoding
	 * is push-based.  In libFLAC, when you ask to decode a frame, the
	 * decoder will eventually call the read callback to supply some data,
	 * but how much it asks for depends on how much free space it has in
	 * its internal buffer.  It does not try to grow its internal buffer
	 * to accommodate a whole frame because then the internal buffer size
	 * could not be limited, which is necessary in embedded applications.
	 *
	 * Ogg however grows its internal buffer until a whole page is present;
	 * only then can you get decoded data out.  So we can't just ask for
	 * the same number of bytes from Ogg, then pass what's decoded down to
	 * libFLAC.  If what libFLAC is asking for will not contain a whole
	 * page, then we will get no data from ogg_sync_pageout(), and at the
	 * same time cannot just read more data from the client for the purpose
	 * of getting a whole decoded page because the decoded size might be
	 * larger than libFLAC's internal buffer.
	 *
	 * Instead, whenever this read callback wrapper is called, we will
	 * continually request data from the client until we have at least one
	 * page, and manage pages internally so that we can send pieces of
	 * pages down to libFLAC in such a way that we obey its size
	 * requirement.  To limit the amount of callbacks, we will always try
	 * to read in enough pages to return the full number of bytes
	 * requested.
	 */
	*bytes = 0;
	while (*bytes < bytes_requested && !aspect->end_of_stream) {
		if (aspect->end_of_link && aspect->have_working_page) {
			/* we've now consumed all packets of this link and have checked that a new page follows it */
			if(*bytes > 0)
				return FLAC__OGG_DECODER_ASPECT_READ_STATUS_OK;
			else
				return FLAC__OGG_DECODER_ASPECT_READ_STATUS_END_OF_LINK;
		}
		else if (aspect->have_working_page) {
			if (aspect->have_working_packet) {
				size_t n = bytes_requested - *bytes;
				if ((size_t)aspect->working_packet.bytes <= n) {
					/* the rest of the packet will fit in the buffer */
					n = aspect->working_packet.bytes;
					memcpy(buffer, aspect->working_packet.packet, n);
					*bytes += n;
					buffer += n;
					aspect->have_working_packet = false;
					if(aspect->working_packet.e_o_s) {
						if(!aspect->decode_chained_stream)
							aspect->end_of_stream = true;
						else {
							aspect->end_of_link = true;
							if(aspect->current_linknumber >= aspect->number_of_links_indexed) {
								FLAC__uint64 tell_offset;
								aspect->linkdetails[aspect->current_linknumber].samples = aspect->working_packet.granulepos;
								aspect->linkdetails[aspect->current_linknumber].serial_number = aspect->serial_number;
								if(tell_callback != 0) {
									if(tell_callback(decoder, &tell_offset, client_data) == FLAC__STREAM_DECODER_TELL_STATUS_OK)
										aspect->linkdetails[aspect->current_linknumber].end_byte = tell_offset - aspect->sync_state.fill + aspect->sync_state.returned;
								}
								aspect->number_of_links_indexed++;

								/* reallocate in chunks of 4 */
								if((aspect->current_linknumber + 1) % 4 == 0) {
									FLAC__OggDecoderAspect_LinkDetails * tmpptr = NULL;
									if(NULL == (tmpptr = safe_realloc_nofree_mul_2op_(aspect->linkdetails,5+aspect->current_linknumber,sizeof(FLAC__OggDecoderAspect_LinkDetails)))) {
										return FLAC__OGG_DECODER_ASPECT_READ_STATUS_MEMORY_ALLOCATION_ERROR;
									}
									aspect->linkdetails = tmpptr;
								}
								memset(&aspect->linkdetails[aspect->current_linknumber+1], 0, sizeof(FLAC__OggDecoderAspect_LinkDetails));
							}
							if(!aspect->is_seeking)
								aspect->need_serial_number = true;
							aspect->have_working_page = false; /* e-o-s packet ends page */
						}
					}
				}
				else {
					/* only n bytes of the packet will fit in the buffer */
					memcpy(buffer, aspect->working_packet.packet, n);
					*bytes += n;
					buffer += n;
					aspect->working_packet.packet += n;
					aspect->working_packet.bytes -= n;
				}
			}
			else {
				/* try and get another packet */
				const int ret = ogg_stream_packetout(&aspect->stream_state, &aspect->working_packet);
				if (ret > 0) {
					aspect->have_working_packet = true;
					/* if it is the first header packet, check for magic and a supported Ogg FLAC mapping version */
					if (aspect->working_packet.bytes > 0 && aspect->working_packet.packet[0] == FLAC__OGG_MAPPING_FIRST_HEADER_PACKET_TYPE) {
						const FLAC__byte *b = aspect->working_packet.packet;
						if (aspect->working_packet.bytes < (long)header_length)
							return FLAC__OGG_DECODER_ASPECT_READ_STATUS_NOT_FLAC;
						b += FLAC__OGG_MAPPING_PACKET_TYPE_LENGTH;
						if (memcmp(b, FLAC__OGG_MAPPING_MAGIC, FLAC__OGG_MAPPING_MAGIC_LENGTH))
							return FLAC__OGG_DECODER_ASPECT_READ_STATUS_NOT_FLAC;
						b += FLAC__OGG_MAPPING_MAGIC_LENGTH;
						aspect->version_major = (uint32_t)(*b);
						b += FLAC__OGG_MAPPING_VERSION_MAJOR_LENGTH;
						aspect->version_minor = (uint32_t)(*b);
						if (aspect->version_major != 1)
							return FLAC__OGG_DECODER_ASPECT_READ_STATUS_UNSUPPORTED_MAPPING_VERSION;
						aspect->working_packet.packet += header_length;
						aspect->working_packet.bytes -= header_length;
					}
				}
				else if (ret == 0) {
					aspect->have_working_page = false;
				}
				else { /* ret < 0 */
					/* lost sync, we'll leave the working page for the next call */
					return FLAC__OGG_DECODER_ASPECT_READ_STATUS_LOST_SYNC;
				}
			}
		}
		else {
			/* try and get another page */
			const int ret = ogg_sync_pageout(&aspect->sync_state, &aspect->working_page);
			if (ret > 0) {
				/* got a page, grab the serial number if necessary */
				if(aspect->need_serial_number) {
					/* Check whether not FLAC. The next if is somewhat confusing: check
					 * whether the length of the next page body agrees with the length
					 * of a FLAC 'header' possibly contained in that page */
					if(aspect->working_page.body_len > 1 + FLAC__OGG_MAPPING_MAGIC_LENGTH &&
					   aspect->working_page.body[0] == FLAC__OGG_MAPPING_FIRST_HEADER_PACKET_TYPE &&
					   memcmp((&aspect->working_page.body) + 1, FLAC__OGG_MAPPING_MAGIC, FLAC__OGG_MAPPING_MAGIC_LENGTH)) {
						aspect->serial_number = ogg_page_serialno(&aspect->working_page);
						ogg_stream_reset_serialno(&aspect->stream_state, aspect->serial_number);
						aspect->need_serial_number = false;

						/* current_linknumber lags a bit: it is only increased after processing
						 * of the whole links is done, while this code does advance processing */
						if(aspect->current_linknumber >= aspect->number_of_links_detected) {
							FLAC__uint64 tell_offset;
							aspect->number_of_links_detected = aspect->current_linknumber + 1;
							if(tell_callback != 0) {
								if(tell_callback(decoder, &tell_offset, client_data) == FLAC__STREAM_DECODER_TELL_STATUS_OK)
									aspect->linkdetails[aspect->current_linknumber].start_byte = tell_offset - aspect->sync_state.fill + aspect->sync_state.returned
									                                                               - aspect->working_page.header_len - aspect->working_page.body_len;
							}
						}
					}
				}
				if(ogg_stream_pagein(&aspect->stream_state, &aspect->working_page) == 0) {
					aspect->have_working_page = true;
					aspect->have_working_packet = false;
				}
				/* else do nothing, could be a page from another stream */
			}
			else if (ret == 0) {
				/* need more data */
				const size_t ogg_bytes_to_read = flac_max(bytes_requested - *bytes, OGG_BYTES_CHUNK);
				char *oggbuf = ogg_sync_buffer(&aspect->sync_state, ogg_bytes_to_read);

				if(0 == oggbuf) {
					return FLAC__OGG_DECODER_ASPECT_READ_STATUS_MEMORY_ALLOCATION_ERROR;
				}
				else {
					size_t ogg_bytes_read = ogg_bytes_to_read;

					switch(read_callback(decoder, (FLAC__byte*)oggbuf, &ogg_bytes_read, client_data)) {
						case FLAC__OGG_DECODER_ASPECT_READ_STATUS_OK:
							break;
						case FLAC__OGG_DECODER_ASPECT_READ_STATUS_END_OF_STREAM:
							aspect->end_of_stream = true;
							break;
						case FLAC__OGG_DECODER_ASPECT_READ_STATUS_ABORT:
							return FLAC__OGG_DECODER_ASPECT_READ_STATUS_ABORT;
						default:
							FLAC__ASSERT(0);
					}

					if(ogg_sync_wrote(&aspect->sync_state, ogg_bytes_read) < 0) {
						/* double protection; this will happen if the read callback returns more bytes than the max requested, which would overflow Ogg's internal buffer */
						FLAC__ASSERT(0);
						return FLAC__OGG_DECODER_ASPECT_READ_STATUS_ERROR;
					}
				}
			}
			else { /* ret < 0 */
				/* lost sync, bail out */
				return FLAC__OGG_DECODER_ASPECT_READ_STATUS_LOST_SYNC;
			}
		}
	}

	if (aspect->end_of_stream && *bytes == 0) {
		return FLAC__OGG_DECODER_ASPECT_READ_STATUS_END_OF_STREAM;
	}

	return FLAC__OGG_DECODER_ASPECT_READ_STATUS_OK;
}
