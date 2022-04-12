/*
 *  Intel AMT IDE redirection protocol helper functions.
 *
 *  Copyright (C) 2022 Hannes Reinecke <hare@suse.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <scsi/scsi.h>
#include "redir.h"

static int ider_data_to_host(struct redir *r, unsigned int seqno,
			     unsigned char device,  unsigned char *data,
			     unsigned int data_len, bool completed)
{
    unsigned char *request;
    int ret;
    unsigned char mask = IDER_STATUS_MASK | IDER_SECTOR_COUNT_MASK |
	IDER_INTERRUPT_MASK;
    struct ider_data_to_host_message msg = {
	.type = IDER_DATA_TO_HOST,
	.attributes = completed ? 2 : 0,
	.input.mask = mask | IDER_BYTE_CNT_LSB_MASK | IDER_BYTE_CNT_MSB_MASK,
	.input.sector_count = IDER_INTERRUPT_IO,
	.input.byte_count_lsb = (data_len & 0xff),
	.input.byte_count_msb = (data_len >> 8) & 0xff,
	.input.drive_select = device,
	.input.status = IDER_STATUS_DRDY | IDER_STATUS_DSC | IDER_STATUS_DRQ,
	.output.mask = mask,
	.output.sector_count = IDER_INTERRUPT_CD | IDER_INTERRUPT_CD,
	.output.drive_select = device,
	.output.status = IDER_STATUS_DRDY | IDER_STATUS_DSC,
    };

    memcpy(&msg.transfer_bytes, &data_len, 2);
    memcpy(&msg.sequence_number, &seqno, 4);
    request = malloc(sizeof(msg) + data_len);
    memcpy(request, &msg, sizeof(msg));
    memcpy(request + sizeof(msg), data, data_len);

    ret = redir_write(r, request, sizeof(msg) + data_len);
    free(request);
    return ret;
}

static int ider_packet_sense(struct redir *r, unsigned int seqno,
			     unsigned char device, unsigned char sense,
			     unsigned char asc, unsigned char asq)
{
    unsigned char mask = IDER_INTERRUPT_MASK | IDER_SECTOR_COUNT_MASK |
	IDER_DRIVE_SELECT_MASK | IDER_STATUS_MASK;
    struct ider_command_response_message msg = {
	.type = IDER_COMMAND_END_RESPONSE,
	.attributes = 2,
	.output.mask = mask,
	.output.sector_count = IDER_INTERRUPT_IO | IDER_INTERRUPT_CD,
	.output.drive_select = device,
	.output.status = IDER_STATUS_DRDY | IDER_STATUS_DSC,
    };
    memcpy(&msg.sequence_number, &seqno, 4);
    if (sense) {
	msg.output.error = (sense << 4);
	msg.output.mask |= IDER_ERROR_MASK;
	msg.output.status |= IDER_STATUS_ERR;
	msg.sense = sense;
	msg.asc = asc;
	msg.asq = asq;
    }
    return redir_write(r, (const char *)&msg, sizeof(msg));
}    

unsigned char ider_mode_page_01_floppy[] = {
    0x00, 0x12, 0x24, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x0A, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00
};
unsigned char ider_mode_page_01_ls120[] = {
    0x00, 0x12, 0x31, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x0A, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00
};
unsigned char ider_mode_page_01_cdrom[] = {
    0x00, 0x0E, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x06, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00
};
unsigned char ider_mode_page_05_floppy[] = {
    0x00, 0x26, 0x24, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x05, 0x1E, 0x04, 0xB0, 0x02, 0x12, 0x02, 0x00,
    0x00, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x02, 0xD0, 0x00, 0x00
};
unsigned char ider_mode_page_05_ls120[] = {
    0x00, 0x26, 0x31, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x05, 0x1E, 0x10, 0xA9, 0x08, 0x20, 0x02, 0x00,
    0x03, 0xC3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x02, 0xD0, 0x00, 0x00
};
unsigned char ider_mode_page_3f_ls120[] =  {
    0x00, 0x5c, 0x24, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x0a, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x03, 0x16, 0x00, 0xa0,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x02, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa0, 0x00,
    0x00, 0x00, 0x05, 0x1E, 0x10, 0xA9, 0x08, 0x20,
    0x02, 0x00, 0x03, 0xC3, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xD0,
    0x00, 0x00, 0x08, 0x0a, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x06,
    0x00, 0x00, 0x00, 0x11, 0x24, 0x31
};
unsigned char ider_mode_page_3f_floppy[] = {
    0x00, 0x5c, 0x24, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x0a, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x03, 0x16, 0x00, 0xa0,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x02, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa0, 0x00,
    0x00, 0x00, 0x05, 0x1e, 0x04, 0xb0, 0x02, 0x12,
    0x02, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xd0,
    0x00, 0x00, 0x08, 0x0a, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x06,
    0x00, 0x00, 0x00, 0x11, 0x24, 0x31
};
unsigned char ider_mode_page_3f_cdrom[] = {
    0x00, 0x28, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x06, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00,
    0x2a, 0x18, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00
};
unsigned char ider_mode_page_1a_cdrom[] = {
    0x00, 0x12, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x1A, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};
unsigned char ider_mode_page_1d_cdrom[] = {
    0x00, 0x12, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x1D, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};
unsigned char ider_mode_page_2a_cdrom[] = {
    0x00, 0x20, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x2a, 0x18, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00
};

int ider_handle_command(struct redir *r, unsigned int seqno,
			unsigned char device, unsigned char *cdb)
{
    unsigned char resp[512];
    unsigned char *mode_sense = NULL;
    uint32_t lba, sector_size, mode_len, resp_len;
    unsigned int lba_size, offset;
    bool last_lba = false;
    int ret = 0;

    if (!r->mmap_size)
	/* NOT READY, MEDIUM NOT PRESENT */
	return ider_packet_sense(r, seqno, device, 0x02, 0x3a, 0x0);

    switch (cdb[0]) {
    case TEST_UNIT_READY:
	return ider_packet_sense(r, seqno, device, 0, 0, 0);
    case MODE_SENSE:
	if (cdb[2] != 0x3f || cdb[3] != 0x00)
	    return ider_packet_sense(r, seqno, device, 0x05, 0x24, 0x00);
	resp[0] = 0;    /* Mode data length */
	resp[1] = 0x05; /* Medium type: CD-ROM data only */
	resp[2] = 0x80; /* device-specific parameters: Write Protect */
	resp[3] = 0;    /* Block-descriptor length */
	return ider_data_to_host(r, seqno, device, resp, 4, true);
    case MODE_SENSE_10:
	mode_len = ((unsigned int)cdb[7] << 8) | (unsigned int)(cdb[8]);
	if (device == 0xa0) {
	    lba = 0;
	} else {
	    lba = (r->mmap_size >> 11);
	}
	switch (cdb[2] & 0x3f) {
	case 0x01:
	    if (device == 0xa0) {
		if (lba < 0xb40)
		    mode_sense = ider_mode_page_01_floppy;
		else
		    mode_sense = ider_mode_page_01_ls120;
	    } else
		mode_sense = ider_mode_page_01_cdrom;
	    break;
	case 0x05:
	    if (device == 0xa0) {
		if (lba < 0xb40)
		    mode_sense = ider_mode_page_05_floppy;
		else
		    mode_sense = ider_mode_page_05_ls120;
	    }
	    break;
	case 0x3f:
	    if (device == 0xa0) {
		if (lba < 0xb40)
		    mode_sense = ider_mode_page_3f_floppy;
		else
		    mode_sense = ider_mode_page_3f_ls120;
	    } else
		mode_sense = ider_mode_page_3f_cdrom;
	    break;
	case 0x1a:
	    if (device == 0xb0)
		mode_sense = ider_mode_page_1a_cdrom;
	    break;
	case 0x1d:
	    if (device == 0xb0)
		mode_sense = ider_mode_page_1d_cdrom;
	    break;
	case 0x2a:
	    if (device == 0xb0)
		mode_sense = ider_mode_page_2a_cdrom;
	    break;
	}
	if (!mode_sense)
	    return ider_packet_sense(r, seqno, device, 0x05, 0x20, 0x00);
	if (mode_len > sizeof(mode_sense))
	    mode_len = sizeof(mode_sense);
	return ider_data_to_host(r, seqno, device,
				 mode_sense, mode_len, true);
    case READ_CAPACITY:
	if (device == 0xa0) {
	    /* NOT READY, MEDIUM NOT PRESENT */
	    return ider_packet_sense(r, seqno, device, 0x02, 0x3a, 0x0);
	}
	lba = (r->mmap_size >> 11) - 1;
	sector_size = (unsigned int)1 << 11;
	resp[0] = (lba >> 24) & 0xff;
	resp[1] = (lba >> 16) & 0xff;
	resp[2] = (lba >>  8) & 0xff;
	resp[3] = lba & 0xff;
	resp[4] = (sector_size >> 24) & 0xff;
	resp[5] = (sector_size >> 16) & 0xff;
	resp[6] = (sector_size >>  8) & 0xff;
	resp[7] = sector_size & 0xff;
	return ider_data_to_host(r, seqno, device, resp, 8, true);
    case READ_10:
	if (device == 0xa0) {
	    /* NOT READY, MEDIUM NOT PRESENT */
	    return ider_packet_sense(r, seqno, device, 0x02, 0x3a, 0x0);
	}
	lba = (unsigned int)cdb[2] << 24 |
	    (unsigned int)cdb[3] << 16 |
	    (unsigned int)cdb[4] << 8 |
	    (unsigned int)cdb[5];
	resp_len = (unsigned int)cdb[7] << 8 | (unsigned int)cdb[8];
	lba_size = 1 << 11;
	last_lba = false;
	for (offset = 0; offset < resp_len; offset += lba_size) {
	    unsigned char *lba_ptr =
		(unsigned char *)r->mmap_buf + lba + offset;
	    if (lba_size >= (resp_len - offset)) {
		lba_size = (resp_len - offset);
		last_lba = true;
	    }
	    ret = ider_data_to_host(r, seqno, device, lba_ptr,
				    lba_size, last_lba);
	    if (ret < 0)
		return ret;
	}
	return ret;
    default:
	break;
    }
    fprintf(stderr, "seqno %u: unhandled command %02x\n", seqno, cdb[0]);
    /* ILLEGAL REQUEST, CDB NOT SUPPORTED */
    return ider_packet_sense(r, seqno, device, 0x05, 0x20, 0x00);
}
