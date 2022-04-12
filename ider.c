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

#include <sys/types.h>
#include <string.h>
#include <scsi/scsi.h>
#include "redir.h"

static int ider_packet_sense(struct redir *r, unsigned int seqno,
			     unsigned char device, unsigned char sense,
			     unsigned char asc, unsigned char asq)
{
    unsigned char mask = IDER_INTERRUPT_MASK | IDER_SECTOR_COUNT_MASK |
	IDER_DRIVE_SELECT_MASK | IDER_STATUS_MASK;
    struct ider_command_response_message msg = {
	.type = IDER_COMMAND_END_RESPONSE,
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

int ider_handle_command(struct redir *r, unsigned int seqno,
			unsigned char *cdb)
{
    unsigned char device = 0xb0;

    if (!r->filename)
	/* NOT READY, MEDIUM NOT PRESENT */
	return ider_packet_sense(r, seqno, device, 0x02, 0x3a, 0x0);

    switch (cdb[0]) {
    case TEST_UNIT_READY:
	return ider_packet_sense(r, seqno, device, 0, 0, 0);
    default:
	break;
    }
    /* ILLEGAL REQUEST, CDB NOT SUPPORTED */
    return ider_packet_sense(r, seqno, device, 0x05, 0x20, 0x00);
}
