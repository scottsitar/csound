/*
    insert.h:

    Copyright (C) 1991, 2002 Barry Vercoe, Istvan Varga

    This file is part of Csound.

    The Csound Library is free software; you can redistribute it
    and/or modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    Csound is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with Csound; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
    02110-1301 USA
*/

#ifndef INSERT_H
#define INSERT_H

#include "csoundCore.h"
#include "udo.h"
#include "aops.h"

typedef struct {                        /*       INSERT.H                */
    OPDS    h;
    LBLBLK  *lblblk;
} GOTO;

typedef struct {
    OPDS    h;
    int32_t     *cond;
    LBLBLK  *lblblk;
} CGOTO;

typedef struct {
    OPDS    h;
    MYFLT   *ndxvar, *incr, *limit;
    LBLBLK  *l;
} LOOP_OPS;

typedef struct {
    OPDS    h;
    MYFLT   *idel, *idur;
    LBLBLK  *lblblk;
    int32   cnt1, cnt2;
} TIMOUT;

typedef struct {
    OPDS    h;
} LINK;

typedef struct {
    OPDS    h;
    INSTANCEREF *inst;
    MYFLT  *ktrig;
} KILLOP;

int32_t kill_instancek(CSOUND *csound, KILLOP *p);


int32 sa_early(CSOUND *csound, AOP *p);
int32 sa_offset(CSOUND *csound, AOP *p);
  
/* the number of optional outputs defined in entry.c */
#define SUBINSTNUMOUTS  8

typedef struct {                        /* IV - Oct 16 2002 */
    OPDS    h;
    MYFLT   *ar[VARGMAX];
    INSDS   *ip, *parent_ip;
    AUXCH   saved_spout;
    OPCOD_IOBUFS    buf;
} SUBINST;

typedef struct {
    OPDS    h;
    MYFLT   *i_ksmps;
} SETKSMPS;

typedef struct {                        /* IV - Oct 20 2002 */
    OPDS    h;
    MYFLT   *i_insno, *iname;
} NSTRNUM;

typedef struct {                        /* JPff Feb 2019 */
    OPDS    h;
    STRINGDAT *ans;
    MYFLT     *num;
} NSTRSTR;

typedef struct {
    OPDS    h;
    MYFLT   *kInsNo, *kFlags, *kRelease;
} TURNOFF2;

typedef struct {
    OPDS    h;
    MYFLT   *insno;
} DELETEIN;

INSDS *instance(CSOUND *, int32_t);

typedef struct {
    OPDS    h;
    MYFLT   *os;
    MYFLT   *in_cvt;
    MYFLT   *out_cvt;
} OVSMPLE;

typedef struct {
  OPDS h;
  INSTANCEREF *out;
  INSTREF *in;
} CREATE_INSTANCE;

typedef struct {
  OPDS h;
  MYFLT *err;
  MYFLT *args[VARGMAX];
} INIT_INSTANCE;

typedef struct {
  OPDS h;
  MYFLT *out;
  INSTANCEREF *in;
} PERF_INSTR;

typedef struct {
  OPDS h;
  INSTANCEREF *in;
} DEL_INSTR;

typedef struct {
  OPDS h;
  MYFLT *out;
  INSTANCEREF *in;
  INSTANCEREF *nxt;
  MYFLT *mode;
} SPLICE_INSTR;

typedef struct {
  OPDS h;
  INSTANCEREF *in;
  MYFLT *pause;
} PAUSE_INSTR;

typedef struct {
  OPDS h;
  INSTANCEREF *in;
  MYFLT *par;
  MYFLT *val;
} PARM_INSTR;

int32_t create_instance_opcode(CSOUND *csound, CREATE_INSTANCE *p);
int32_t init_instance_opcode(CSOUND *csound, INIT_INSTANCE *p);
int32_t perf_instance_opcode(CSOUND *csound, PERF_INSTR *p);
int32_t delete_instance_opcode(CSOUND *csound, DEL_INSTR *p);
int32_t pause_instance_opcode(CSOUND *csound, PAUSE_INSTR *p);
int32_t set_instance_parameter(CSOUND *csound, PARM_INSTR *p);
int32_t get_instance(CSOUND *csound, DEL_INSTR *p);
int32_t splice_instance(CSOUND *csound, SPLICE_INSTR *p);

#endif
