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

static int ider_data_to_host(struct redir *r, unsigned char device,
			     unsigned char *data, unsigned int data_len,
			     bool completed, bool dma)
{
    unsigned char *request;
    int ret;
    unsigned char mask = IDER_STATUS_MASK | IDER_SECTOR_COUNT_MASK;
    struct ider_data_to_host_message msg = {
	.type = IDER_DATA_TO_HOST,
	.attributes = completed ? 2 : 0,
	.input.mask = mask | IDER_BYTE_CNT_LSB_MASK | IDER_BYTE_CNT_MSB_MASK,
	.input.sector_count = IDER_INTERRUPT_IO,
	.input.drive_select = device,
	.input.status = IDER_STATUS_DRDY | IDER_STATUS_DSC | IDER_STATUS_DRQ,
    };

    memcpy(&msg.transfer_bytes, &data_len, 2);
    memcpy(&msg.sequence_number, &r->seqno, 4);
    if (!dma) {
	msg.input.mask |= IDER_INTERRUPT_MASK;
    } else {
	msg.input.byte_count_lsb = (data_len & 0xff);
	msg.input.byte_count_msb = (data_len >> 8) & 0xff;
    }
    if (completed) {
	msg.output.mask = mask | IDER_INTERRUPT_MASK;
	msg.output.sector_count = IDER_INTERRUPT_CD | IDER_INTERRUPT_IO;
	msg.output.drive_select = device;
	msg.output.status = IDER_STATUS_DRDY | IDER_STATUS_DSC;
    }
    request = malloc(sizeof(msg) + data_len);
    memcpy(request, &msg, sizeof(msg));
    memcpy(request + sizeof(msg), data, data_len);

    ret = redir_write(r, request, sizeof(msg) + data_len);
    free(request);
    return ret;
}

static int ider_packet_sense(struct redir *r,
			     unsigned char device, unsigned char sense,
			     unsigned char asc, unsigned char asq)
{
    unsigned char mask = IDER_INTERRUPT_MASK | IDER_SECTOR_COUNT_MASK |
	IDER_STATUS_MASK; /* | IDER_DRIVE_SELECT_MASK */
    struct ider_command_response_message msg = {
	.type = IDER_COMMAND_END_RESPONSE,
	.attributes = 2,
	.output.mask = mask,
	.output.sector_count = IDER_INTERRUPT_IO | IDER_INTERRUPT_CD,
	.output.drive_select = device,
	.output.status = IDER_STATUS_DRDY | IDER_STATUS_DSC,
    };
    memcpy(&msg.sequence_number, &r->seqno, 4);
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

static int ider_read_data(struct redir *r, unsigned char device, bool use_dma,
			  unsigned long lba, unsigned int count)
{
    off_t mmap_offset = (off_t)lba << r->lba_shift;
    size_t mmap_len = (size_t)count << r->lba_shift;
    unsigned char *lba_ptr =
	(unsigned char *)r->mmap_buf + mmap_offset;
    size_t chunk_size = 0x2000;
    bool last_lba = false;
    int ret = 0;

    if (!count)
	return ider_packet_sense(r, device, 0x00, 0x00, 0x00);
    if (mmap_offset >= r->mmap_size)
	return ider_packet_sense(r, device, 0x05, 0x21, 0x00);

    while (mmap_len) {
	if (mmap_len <= chunk_size) {
	    chunk_size = mmap_len;
	    last_lba = true;
	}
	ret = ider_data_to_host(r, device, lba_ptr, chunk_size,
				last_lba, use_dma);
	if (ret < 0)
	    break;
	if (last_lba)
	    break;
	lba_ptr += chunk_size;
	mmap_len -= chunk_size;
    }
    return ret;
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

unsigned char ider_config_feature_0000[] = {
    0x00, 0x00, /* Feature Code 0000: profile list */
    0x03, 0x04, /* Ver=0, Persist=1, Current = 1 Additional Len = 4 */
    0x00, 0x08, 0x01, 0x00, /* Current profile: 0008 - CD-ROM */
};
unsigned char ider_config_feature_0001[] = {
    0x00, 0x01, /* Feature Code 0001: core */
    0x03, 0x04, /* Ver=0, Persist=1, Current = 1 Additional Len = 4 */
    0x00, 0x00, 0x00, 0x02 /* Physical Interface Standard = ATAPI */
};
unsigned char ider_config_feature_0002[] = {
    0x00, 0x02, /* Feature Code 0002h = Morphing */
    0x03, 0x04, /* Ver=0, Persist=1, Current = 1 Additional Len = 4 */
    0x00, 0x00, 0x00, 0x00 /* Async = 0 */
};
unsigned char ider_config_feature_0003[] = {
    0x00, 0x03, /* Feature Code 0003h = Removeable Medium */
    0x03, 0x04, /* Ver=0, Persist=1, Current = 1 Additional Len = 4 */
    0x29, 0x00, 0x00, 0x02, /* Loading mechanism type = 1 (Tray), Eject = 1, Lock = 1 */
};
unsigned char ider_config_feature_0010[] = {
    0x00, 0x10, /* Feature Code 0010h = Random Readable */
    0x01, 0x08, /* Ver=0, Persist=0, Current = 1 Additional Len = 8 */
    0x00, 0x00, 0x08, 0x00, /* Logical block size = 2048 */
    0x00, 0x01, /* Blocking = 1 */
    0x00, 0x00  /* Read/Write Error Recovery page = 0 */
};
unsigned char ider_config_feature_001e[] = {
    0x00, 0x1E, /* Feature Code 001Eh = CD Read */
    0x03, 0x00, /* Ver=0, Persist=1, Current = 1, Additional Len = 0 */
};
unsigned char ider_config_feature_0100[] = {
    0x01, 0x00, /* Feature Code 0100h = Power Management */
    0x03, 0x00  /* Ver=0, Persist=1, Current = 1, Additional Len = 0 */
};
unsigned char ider_config_feature_0105[] = {
    0x01, 0x05, /* Feature Code 0105h = Timeout */
    0x03, 0x00  /* Ver=0, Persist=1, Current = 1, Additional Len = 0 */
};

int ider_handle_command(struct redir *r, unsigned int seqno,
			unsigned char device, bool use_dma,
			unsigned char *cdb)
{
    unsigned char resp[512];
    unsigned char *mode_sense = NULL, format;
    uint32_t lba, mode_len;
    unsigned int count, resp_len = 0, resp_offset;
    unsigned int start_feature;

    if (!r->mmap_size || device != r->device)
	/* NOT READY, MEDIUM NOT PRESENT */
	return ider_packet_sense(r, device, 0x02, 0x3a, 0x0);

    switch (cdb[0]) {
    case TEST_UNIT_READY:
	fprintf(stderr, "seqno %u: dev %02x test unit ready\n",
		seqno, device);
	return ider_packet_sense(r, device, 0, 0, 0);
    case ALLOW_MEDIUM_REMOVAL:
	fprintf(stderr, "seqno %u: dev %02x %s medium removal\n",
		seqno, device, cdb[4] & 1 ? "prevent" : "allow");
	return ider_packet_sense(r, device, 0, 0, 0);
    case MODE_SENSE:
	fprintf(stderr, "seqno %u: dev %02x mode sense pg %02x\n",
		seqno, device, cdb[2]);
	if (cdb[2] != 0x3f || cdb[3] != 0x00)
	    return ider_packet_sense(r, device, 0x05, 0x24, 0x00);
	resp[0] = 0;    /* Mode data length */
	resp[1] = 0x05; /* Medium type: CD-ROM data only */
	resp[2] = 0x80; /* device-specific parameters: Write Protect */
	resp[3] = 0;    /* Block-descriptor length */
	return ider_data_to_host(r, device, resp, 4, true, use_dma);
    case MODE_SENSE_10:
	mode_len = ((unsigned int)cdb[7] << 8) | (unsigned int)(cdb[8]);
	lba = (r->mmap_size >> r->lba_shift);
	fprintf(stderr, "seqno %u: mode sense pg %02x len %u\n",
		seqno, cdb[2], mode_len);
	switch (cdb[2] & 0x3f) {
	case 0x01:
	    if (device == IDER_DEVICE_FLOPPY) {
		if (lba < 0xb40)
		    mode_sense = ider_mode_page_01_floppy;
		else
		    mode_sense = ider_mode_page_01_ls120;
	    } else
		mode_sense = ider_mode_page_01_cdrom;
	    break;
	case 0x05:
	    if (device == IDER_DEVICE_FLOPPY) {
		if (lba < 0xb40)
		    mode_sense = ider_mode_page_05_floppy;
		else
		    mode_sense = ider_mode_page_05_ls120;
	    }
	    break;
	case 0x3f:
	    if (device == IDER_DEVICE_FLOPPY) {
		if (lba < 0xb40)
		    mode_sense = ider_mode_page_3f_floppy;
		else
		    mode_sense = ider_mode_page_3f_ls120;
	    } else
		mode_sense = ider_mode_page_3f_cdrom;
	    break;
	case 0x1a:
	    if (device == IDER_DEVICE_CDROM)
		mode_sense = ider_mode_page_1a_cdrom;
	    break;
	case 0x1d:
	    if (device == IDER_DEVICE_CDROM)
		mode_sense = ider_mode_page_1d_cdrom;
	    break;
	case 0x2a:
	    if (device == IDER_DEVICE_CDROM)
		mode_sense = ider_mode_page_2a_cdrom;
	    break;
	}
	if (!mode_sense)
	    return ider_packet_sense(r, device, 0x05, 0x20, 0x00);
	if (mode_len > sizeof(mode_sense))
	    mode_len = sizeof(mode_sense);
	return ider_data_to_host(r, device, mode_sense, mode_len, true, use_dma);
    case READ_CAPACITY:
	lba = (r->mmap_size >> r->lba_shift) - 1;
	resp[0] = (lba >> 24) & 0xff;
	resp[1] = (lba >> 16) & 0xff;
	resp[2] = (lba >>  8) & 0xff;
	resp[3] = lba & 0xff;
	resp[4] = (r->lba_size >> 24) & 0xff;
	resp[5] = (r->lba_size >> 16) & 0xff;
	resp[6] = (r->lba_size >>  8) & 0xff;
	resp[7] = r->lba_size & 0xff;
	fprintf(stderr, "seqno %u: read capacity size %u block size %u\n",
		seqno, lba, r->lba_size);
	return ider_data_to_host(r, device, resp, 8, true, use_dma);
    case READ_TOC:
	if (device == IDER_DEVICE_FLOPPY) {
	    /* ILLEGAL REQUEST, INVALID COMMAND OPERATION CODE */
	    return ider_packet_sense(r, device, 0x05, 0x20, 0x00);
	} else {
	    bool msf;

	    resp_len = (unsigned int)cdb[7] << 8 | cdb[8];
	    format = cdb[2] & 0x0f;
	    msf = cdb[1] & 0x02;
	    fprintf(stderr, "seqno %u: read toc format %u msf %u len %u\n",
		    seqno, format, msf, resp_len);
	    if (format != 0 && format != 1) {
		/* CHECK CONDITION, INVALID FIELD IN CDB */
		return ider_packet_sense(r, device, 0x05, 0x24, 0x00);
	    }
	    if (format == 0) {
		memset(resp, 0x0, 0x14);
		if (resp_len > 0x14)
		    resp_len = 0x14;
		resp[0] = 0x00; /* Data length: MSB */
		resp[1] = 0x12; /* Data length: LSB */
		resp[2] = 0x01; /* First track number */
		resp[3] = 0x01; /* Last track number */
		resp[5] = 0x14; /* ADR: 0x01, CONTROL: 0x04 */
		resp[6] = 0x01; /* Track Number: 1 */
		/* Track 1 start address */
		resp[13] = 0x14; /* ADR: 0x01, CONTROL: 0x04 */
		resp[14] = 0xaa; /* Track Number: Start of lead-out */
		/* Track 2 start address */
		if (msf) {
		    resp[8] = 0x00; /* Track start address */
		    resp[9] = 0x00;
		    resp[10] = 0x02;
		    resp[11] = 0x00;
		    resp[16] = 0x00; /* Track start address */
		    resp[17] = 0x00;
		    resp[18] = 0x34;
		    resp[19] = 0x13;
		}
		/* All zero track addresses for non-MSF */
	    } else {
		if (resp_len > 0x0c)
		    resp_len = 0x0c;
		resp[0] = 0x00; /* Data length: MSB */
		resp[1] = 0x0a; /* Data length: LSB */
		resp[2] = 0x01; /* First complete session */
		resp[3] = 0x01; /* Last complete session */
		resp[4] = 0x00; /* Reserved */
		resp[5] = 0x14; /* ADR: 0x01, CONTROL: 0x04 */
		resp[6] = 0x01; /* First track in last complete session */
	    }
	    return ider_data_to_host(r, device, resp, resp_len, true, use_dma);
	}
	break;
    case 0x46: /* GET CONFIGURATION */
	start_feature = cdb[2];
	resp_len = (unsigned int)cdb[7] << 8 | cdb[8];
	fprintf(stderr, "seqno %u: get_configuration start %u len %u\n",
		seqno, start_feature, resp_len);
	if (resp_len > sizeof(resp_len))
	    resp_len = sizeof(resp_len);
	resp_offset = 6;
	while (start_feature < 8) {
	    unsigned char *feature = NULL;

	    switch (start_feature) {
	    case 0:
		feature = ider_config_feature_0000;
		break;
	    case 1:
		feature = ider_config_feature_0001;
		break;
	    case 2:
		feature = ider_config_feature_0002;
		break;
	    case 3:
		feature = ider_config_feature_0003;
		break;
	    case 4:
		feature = ider_config_feature_0010;
		break;
	    case 5:
		feature = ider_config_feature_001e;
		break;
	    case 6:
		feature = ider_config_feature_0100;
		break;
	    case 7:
		feature = ider_config_feature_0105;
		break;
	    default:
		break;
	    }
	    if (!feature) {
		/* CHECK CONDITION, INVALID FIELD IN CDB */
		return ider_packet_sense(r, device, 0x05, 0x24, 0x00);
	    }
	    memcpy(resp + resp_offset, feature, feature[3] + 4);
	    resp_offset += feature[3] + 4;
	    start_feature++;
	}
	resp[0] = (resp_offset >> 24) & 0xff;
	resp[1] = (resp_offset >> 16) & 0xff;
	resp[2] = (resp_offset >>  8) & 0xff;
	resp[3] = resp_offset & 0xff;
	if (resp_len > resp_offset)
	    resp_len = resp_offset;
	return ider_data_to_host(r, device, resp, resp_len, true, use_dma);
    case 0x51: /* READ DISC INFORMATION, missing from scsi.h */
	format = cdb[1] & 0x7;
	resp_len = (unsigned int)cdb[7] << 8 | cdb[8];
	fprintf(stderr, "seqno %u: read disc information format %u len %u\n",
		seqno, format, resp_len);
	if (format != 0) {
		/* CHECK CONDITION, INVALID FIELD IN CDB */
		return ider_packet_sense(r, device, 0x05, 0x24, 0x00);
	}
	if (resp_len > sizeof(resp))
	    resp_len = sizeof(resp);
	if (resp_len < 34)
	    resp_len = 34;
	memset(resp, 0x0, resp_len);
	resp[0] = (resp_len >> 8) & 0xff;
	resp[1] = (resp_len & 0xff);
	resp[2] = 0x0e;
	return ider_data_to_host(r, device, resp, resp_len, true, use_dma);
    case 0x52: /* READ TRACK INFORMATION */
	format = cdb[1] & 0x3;
	lba = (unsigned int)cdb[2] << 24 |
	    (unsigned int)cdb[3] << 16 |
	    (unsigned int)cdb[4] << 8 |
	    (unsigned int)cdb[5];
	resp_len = (unsigned int)cdb[7] << 8 | (unsigned int)cdb[8];
	fprintf(stderr, "seqno %u: read track information type %u lba %u\n",
		seqno, format, lba);
	if (resp_len < 48)
	    resp_len = 48;
	if (resp_len > sizeof(resp))
	    resp_len = sizeof(resp);
	memset(resp, 0x0, resp_len);
	resp[0] = (resp_len >> 8) & 0xff;
	resp[1] = (resp_len & 0xff);
	return ider_data_to_host(r, device, resp, resp_len, true, use_dma);
    case READ_10:
	lba = (unsigned int)cdb[2] << 24 |
	    (unsigned int)cdb[3] << 16 |
	    (unsigned int)cdb[4] << 8 |
	    (unsigned int)cdb[5];
	count = (unsigned int)cdb[7] << 8 | (unsigned int)cdb[8];
	fprintf(stderr, "seqno %u: read lba %u count %u\n", seqno, lba, count);
	return ider_read_data(r, device, use_dma, lba, count);
    default:
	break;
    }
    fprintf(stderr, "seqno %u: unhandled command %02x\n", seqno, cdb[0]);
    /* ILLEGAL REQUEST, INVALID COMMAND OPERATION CODE */
    return ider_packet_sense(r, device, 0x05, 0x20, 0x00);
}
