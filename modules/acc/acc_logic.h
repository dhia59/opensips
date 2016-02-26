/*
 * Accounting module
 *
 * Copyright (C) 2006 Voice Sistem SRL
 *
 * This file is part of opensips, a free SIP server.
 *
 * opensips is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * opensips is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 *
 * History:
 * ---------
 * 2005-09-19  created during a big re-structuring of acc module(bogdan)
 */


#ifndef _ACC_ACC_LOGIC_H
#define _ACC_ACC_LOGIC_H

#include "../../str.h"
#include "../tm/t_hooks.h"
#include "../dialog/dlg_cb.h"


/* various acc variables */
struct acc_enviroment {
	unsigned int code;
	str code_s;
	str reason;
	struct hdr_field *to;
	str text;
	struct timeval ts;
  event_id_t event;
};

/* param trasnporter*/
struct acc_param {
	int code;
	str code_s;
	str reason;
};


void acc_onreq( struct cell* t, int type, struct tmcb_params *ps );

int w_acc_log_request(struct sip_msg *rq, pv_elem_t* comment, char *foo);

int w_acc_aaa_request(struct sip_msg *rq, pv_elem_t* comment, char *foo);

int w_acc_db_request(struct sip_msg *rq, pv_elem_t* comment, char *table);

int acc_pvel_to_acc_param(struct sip_msg *rq, pv_elem_t* pv_el, struct acc_param* accp);

void acc_loaded_callback(struct dlg_cell *dlg, int type,
			struct dlg_cb_params *_params);

#ifdef DIAM_ACC
int w_acc_diam_request(struct sip_msg *rq, char *comment, char *foo);
#endif

int w_acc_evi_request(struct sip_msg *rq, pv_elem_t* comment, char *foo);

#endif
