/*
 * Csound.c: csound engine initialisation and setup
 *
 *
 * Copyright (C) 2001-2006 Michael Gogins, Matt Ingalls, John D. Ramsdell,
 *                         John P. ffitch, Istvan Varga, Victor Lazzarini,
 *                         Steven Yi
 *
 * License
 *
 * This software is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this software; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#if defined(HAVE_UNISTD_H) || defined(__unix) || defined(__unix__)
#include <unistd.h>
#endif

#include "csoundCore.h"
#include "csmodule.h"
#include "corfile.h"
#include "csound_standard_types.h"
#include "csGblMtx.h"
#include "fftlib.h"
#include <stdio.h>
#include <stdarg.h>
#include <signal.h>
#include <time.h>
#include <ctype.h>
#include <limits.h>
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#if defined(WIN32) && !defined(__CYGWIN__)
#include <winsock2.h>
#include <windows.h>
#endif
#include <math.h>
#include "oload.h"
#include "fgens.h"
#include "namedins.h"
#include "pvfileio.h"
#include "fftlib.h"
#include "lpred.h"
#include "cs_par_base.h"
#include "cs_par_orc_semantics.h"
#include "namedins.h"
// #include "cs_par_dispatch.h"
#include "find_opcode.h"

#if defined(linux) || defined(__HAIKU__) || defined(__EMSCRIPTEN__) ||         \
    defined(__CYGWIN__)
#define PTHREAD_SPINLOCK_INITIALIZER 0
#endif

#include "csound_standard_types.h"

#include "csdebug.h"
#include <time.h>

int32_t kperf_nodebug(CSOUND *csound);
uint32_t csoundGetNchnls(CSOUND *);
uint32_t csoundGetNchnlsInput(CSOUND *csound);
long csoundGetInputBufferSize(CSOUND *);
long csoundGetOutputBufferSize(CSOUND *);
void *csoundGetNamedGens(CSOUND *);
int32_t *csoundGetChannelLock(CSOUND *csound, const char *name);
int32_t csoundCompileCsd(CSOUND *csound, const char *csd_filename);
int32_t csoundCompileCsdText(CSOUND *csound, const char *csd_text);
int32_t csoundCleanup(CSOUND *);
void csoundInputMessage(CSOUND *csound, const char *sc);
int32_t csoundScoreEvent(CSOUND *, char type, const MYFLT *pFields,
                         long numFields);

extern void allocate_message_queue(CSOUND *csound);
int32_t playopen_dummy(CSOUND *, const csRtAudioParams *parm);
void rtplay_dummy(CSOUND *, const MYFLT *outBuf, int32_t nbytes);
int32_t recopen_dummy(CSOUND *, const csRtAudioParams *parm);
int32_t rtrecord_dummy(CSOUND *, MYFLT *inBuf, int32_t nbytes);
void rtclose_dummy(CSOUND *);
int32_t audio_dev_list_dummy(CSOUND *, CS_AUDIODEVICE *, int32_t);
int32_t midi_dev_list_dummy(CSOUND *, CS_MIDIDEVICE *, int32_t);
static void csoundDefaultMessageCallback(CSOUND *, int32_t, const char *,
                                         va_list);
static int32_t defaultCsoundYield(CSOUND *);
static int32_t csoundDoCallback_(CSOUND *, void *, uint32_t);
static void reset(CSOUND *);
void csoundTableSetInternal(CSOUND *csound, int32_t table, int32_t index,
                            MYFLT value);
static INSTRTXT **csoundGetInstrumentList(CSOUND *csound);
uint64_t csoundGetKcounter(CSOUND *csound);
static void set_util_sr(CSOUND *csound, MYFLT sr);
static void set_util_nchnls(CSOUND *csound, int32_t nchnls);

extern void cscoreRESET(CSOUND *);
extern void memRESET(CSOUND *);
extern MYFLT csoundPow2(CSOUND *csound, MYFLT a);
extern int32_t csoundInitStaticModules(CSOUND *);
extern void close_all_files(CSOUND *);
extern void csoundInputMessageInternal(CSOUND *csound, const char *message);
extern int32_t isstrcod(MYFLT);
extern int32_t fterror(const FGDATA *ff, const char *s, ...);
PUBLIC int32_t csoundErrCnt(CSOUND *);
void (*msgcallback_)(CSOUND *, int32_t, const char *, va_list) = NULL;
INSTRTXT *csoundGetInstrument(CSOUND *csound, int32_t insno, const char *name);
void *csoundDCTSetup(CSOUND *csound, int32_t FFTsize, int32_t d);
void csoundDCT(CSOUND *csound, void *p, MYFLT *sig);
void csoundDebuggerBreakpointReached(CSOUND *csound);
void message_dequeue(CSOUND *csound);

int32_t csoundCompileTreeInternal(CSOUND *csound, TREE *root, int32_t async);
int32_t csoundCompileOrcInternal(CSOUND *csound, const char *str,
                                 int32_t async);
int32_t csoundReadScoreInternal(CSOUND *csound, const char *message);
int32_t csoundScoreEventInternal(CSOUND *csound, char type,
                                 const MYFLT *pfields, long numFields);
void csoundScoreEventAsync(CSOUND *csound, char type, const MYFLT *pfields,
                           long numFields);
void csoundReadScoreAsync(CSOUND *csound, const char *message);

extern OENTRY opcodlst_1[];

#define STRING_HASH(arg) STRSH(arg)
#define STRSH(arg) #arg

static const char *csoundGetStrsets(CSOUND *csound, int32_t n) {
  if (csound->strsets == NULL)
    return NULL;
  else
    return csound->strsets[n];
}

static int32_t csoundGetStrsetsMax(CSOUND *csound) {
  return csound->strsmax;
}

static const char *csoundFileError(CSOUND *csound, void *ff) {
  CSFILE *f = (CSFILE *)ff;
  switch (f->type) {
  case CSFILE_SND_W:
  case CSFILE_SND_R:
    return csound->SndfileStrError(csound, ff);
    break;
  default:
    return "";
  }
}

void print_csound_version(CSOUND *csound) {
#ifdef USE_DOUBLE
#ifdef BETA
  csoundErrorMsg(csound,
                 Str("--Csound version %s beta (double samples) %s\n"
                     "[commit: %s]\n"),
                 CS_PACKAGE_VERSION, CS_PACKAGE_DATE,
                 STRING_HASH(GIT_HASH_VALUE));
#else
  csoundErrorMsg(csound,
                 Str("--Csound version %s (double samples) %s\n"
                     "[commit: %s]\n"),
                 CS_PACKAGE_VERSION, CS_PACKAGE_DATE,
                 STRING_HASH(GIT_HASH_VALUE));
#endif
#else
#ifdef BETA
  csoundErrorMsg(csound,
                 Str("--Csound version %s beta (float samples) %s\n"
                     "[commit: %s]\n"),
                 CS_PACKAGE_VERSION, CS_PACKAGE_DATE,
                 STRING_HASH(GIT_HASH_VALUE));
#else
  csoundErrorMsg(csound,
                 Str("--Csound version %s (float samples) %s\n"
                     "[commit: %s]\n"),
                 CS_PACKAGE_VERSION, CS_PACKAGE_DATE,
                 STRING_HASH(GIT_HASH_VALUE));
#endif
#endif
}

void print_sndfile_version(CSOUND *csound) {
#ifdef USE_LIBSNDFILE
  char buffer[128];
  csound->SndfileCommand(csound, NULL, SFC_GET_LIB_VERSION, buffer, 128);
  csoundErrorMsg(csound, "%s\n", buffer);
#else
  csoundErrorMsg(csound, "%s\n", "No soundfile IO");
#endif
}

void print_engine_parameters(CSOUND *csound) {
  csoundErrorMsg(csound, Str("sr = %.1f,"), csound->esr);
  csoundErrorMsg(csound, Str(" kr = %.3f,"), csound->ekr);
  csoundErrorMsg(csound, Str(" ksmps = %d\n"), csound->ksmps);
  csoundErrorMsg(csound, Str("0dBFS level = %.1f,"), csound->e0dbfs);
  csoundErrorMsg(csound, Str(" A4 tuning = %.1f\n"), csound->A4);
}

static void free_opcode_table(CSOUND *csound) {
  int32_t i;
  CS_HASH_TABLE_ITEM *bucket;
  CONS_CELL *head;

  for (i = 0; i < csound->opcodes->table_size; i++) {
    bucket = csound->opcodes->buckets[i];

    while (bucket != NULL) {
      head = bucket->value;
      cs_cons_free_complete(csound, head);
      bucket = bucket->next;
    }
  }

  cs_hash_table_free(csound, csound->opcodes);
}
static void create_opcode_table(CSOUND *csound) {

  int32_t err;

  if (csound->opcodes != NULL) {
    free_opcode_table(csound);
  }
  csound->opcodes = cs_hash_table_create(csound);

  /* Basic Entry1 stuff */
  err = csoundAppendOpcodes(csound, &(opcodlst_1[0]), -1);

  if (UNLIKELY(err))
    csoundDie(csound, Str("Error allocating opcode list"));
}

static int64_t sndfileWrite(CSOUND *csound, void *h, MYFLT *p, int64_t frames) {
  IGN(csound);
  return sflib_writef_MYFLT(h, p, frames);
}

static int64_t sndfileRead(CSOUND *csound, void *h, MYFLT *p, int64_t frames) {
  IGN(csound);
  return sflib_readf_MYFLT(h, p, frames);
}

static int64_t sndfileWriteSamples(CSOUND *csound, void *h, MYFLT *p,
                                   int64_t samples) {
  IGN(csound);
  return sflib_write_MYFLT(h, p, samples);
}

static int64_t sndfileReadSamples(CSOUND *csound, void *h, MYFLT *p,
                                  int64_t samples) {
  IGN(csound);
  return sflib_read_MYFLT(h, p, samples);
}

static int64_t sndfileSeek(CSOUND *csound, void *h, int64_t frames,
                           int32_t whence) {
  IGN(csound);
  return sflib_seek(h, frames, whence);
}

static void *sndfileOpen(CSOUND *csound, const char *path, int32_t mode,
                         SFLIB_INFO *sfinfo) {
  IGN(csound);
  return sflib_open(path, mode, sfinfo);
}

static void *sndfileOpenFd(CSOUND *csound, int32_t fd, int32_t mode,
                           SFLIB_INFO *sfinfo, int32_t close_desc) {
  IGN(csound);
  return sflib_open_fd(fd, mode, sfinfo, close_desc);
}

static int32_t sndfileClose(CSOUND *csound, void *sndfile) {
  IGN(csound);
  return sflib_close(sndfile);
}

static int32_t sndfileSetString(CSOUND *csound, void *sndfile, int32_t str_type,
                                const char *str) {
  IGN(csound);
  return sflib_set_string(sndfile, str_type, str);
}

static const char *sndfileStrError(CSOUND *csound, void *sndfile) {
  IGN(csound);
  return sflib_strerror(sndfile);
}

static int32_t sndfileCommand(CSOUND *csound, void *handle, int32_t cmd,
                              void *data, int32_t datasize) {
  return sflib_command(handle, cmd, data, datasize);
}

// stubs

static int64_t sndfileWrite_stub(CSOUND *csound, void *h, MYFLT *p, int64_t frames) {
  return 0;
}

static int64_t sndfileRead_stub(CSOUND *csound, void *h, MYFLT *p, int64_t frames) {
  return 0;
}

static int64_t sndfileWriteSamples_stub(CSOUND *csound, void *h, MYFLT *p,
                                   int64_t samples) {
  return 0;
}

static int64_t sndfileReadSamples_stub(CSOUND *csound, void *h, MYFLT *p,
                                  int64_t samples) {
  return 0;
}

static int64_t sndfileSeek_stub(CSOUND *csound, void *h, int64_t frames,
                           int32_t whence) {
  return 0;
}

static void *sndfileOpen_stub(CSOUND *csound, const char *path, int32_t mode,
                         SFLIB_INFO *sfinfo) {
  return 0;
}

static void *sndfileOpenFd_stub(CSOUND *csound, int32_t fd, int32_t mode,
                           SFLIB_INFO *sfinfo, int32_t close_desc) {
  return 0;
}

static int32_t sndfileClose_stub(CSOUND *csound, void *sndfile) {
  return 0;
}

static int32_t sndfileSetString_stub(CSOUND *csound, void *sndfile, int32_t str_type,
                                const char *str) {
  return 0;
}

static const char *sndfileStrError_stub(CSOUND *csound, void *sndfile) {
  return NULL;
}

static int32_t sndfileCommand_stub(CSOUND *csound, void *handle, int32_t cmd,
                              void *data, int32_t datasize) {
  return 0;
}

/** Sets the callbacks for sndfile IO
    NULL callbacks are replaced by stubs.
 */
PUBLIC void csoundSetSndfileCallbacks(CSOUND *csound, SNDFILE_CALLBACKS *p) {
  if (p == NULL) {
    csound->SndfileOpen = sndfileOpen;
    csound->SndfileOpenFd = sndfileOpenFd;
    csound->SndfileClose = sndfileClose;
    csound->SndfileWrite = sndfileWrite;
    csound->SndfileRead = sndfileRead;
    csound->SndfileWriteSamples = sndfileWriteSamples;
    csound->SndfileReadSamples = sndfileReadSamples;
    csound->SndfileSeek = sndfileSeek;
    csound->SndfileSetString = sndfileSetString;
    csound->SndfileStrError = sndfileStrError;
    csound->SndfileCommand = sndfileCommand;
  } else {
    csound->SndfileOpen = p->sndfileOpen ? p->sndfileOpen : sndfileOpen_stub;
    csound->SndfileOpenFd = p->sndfileOpenFd ? p->sndfileOpenFd : sndfileOpenFd_stub;
    csound->SndfileClose = p->sndfileClose ? p->sndfileClose : sndfileClose_stub;
    csound->SndfileWrite = p->sndfileWrite ? p->sndfileWrite : sndfileWrite_stub;
    csound->SndfileRead = p->sndfileRead ? p->sndfileRead : sndfileRead_stub;
    csound->SndfileWriteSamples =
        p->sndfileWriteSamples ? p->sndfileWriteSamples : sndfileWriteSamples_stub;
    csound->SndfileReadSamples =
        p->sndfileReadSamples ? p->sndfileReadSamples : sndfileReadSamples_stub;
    csound->SndfileSeek = p->sndfileSeek ? p->sndfileSeek : sndfileSeek_stub;
    csound->SndfileSetString =
        p->sndfileSetString ? p->sndfileSetString : sndfileSetString_stub;
    csound->SndfileStrError =
        p->sndfileStrError ? p->sndfileStrError : sndfileStrError_stub;
    csound->SndfileCommand =
        p->sndfileCommand ? p->sndfileCommand : sndfileCommand_stub;
  }
}

#define MAX_MODULES 64

static void module_list_add(CSOUND *csound, char *drv, char *type) {
  MODULE_INFO **modules =
      (MODULE_INFO **)csoundQueryGlobalVariable(csound, "_MODULES");
  if (modules != NULL) {
    int32_t i = 0;
    while (modules[i] != NULL && i < MAX_MODULES) {
      if (!strcmp(modules[i]->module, drv))
        return;
      i++;
    }
    modules[i] = (MODULE_INFO *)csound->Malloc(csound, sizeof(MODULE_INFO));
    strNcpy(modules[i]->module, drv, 11);
    strNcpy(modules[i]->type, type, 11);
  }
}

static int32_t csoundGetRandSeed(CSOUND *csound, int32_t which) {
  if (which > 1)
    return csound->randSeed1;
  else
    return csound->randSeed2;
}

static int32_t *RandSeed1(CSOUND *csound) {
  return &(csound->randSeed1);
}

static int32_t csoundGetDitherMode(CSOUND *csound) {
  return csound->dither_output;
}

static int32_t csoundGetReinitFlag(CSOUND *csound) {
  return csound->reinitflag;
}

static int32_t csoundGetTieFlag(CSOUND *csound) {
  return csound->tieflag;
}

MYFLT csoundSystemSr(CSOUND *csound, MYFLT val) {
  if (val > 0)
    csound->_system_sr = val;
  return csound->_system_sr;
}

/* get type from name */
PUBLIC const CS_TYPE *GetType(CSOUND *csound, const char *type) {
  return csoundGetTypeWithVarTypeName(csound->typePool, type);
}

const CSOUND_UTIL *csoundGetCsoundUtility(CSOUND *csound) {
  return &csound->csound_util;
}

/*
  exact selects the type of search
 */
static const OENTRY *csoundFindOpcode(CSOUND *csound, int32_t exact,
                                      char *opname, char *outargs,
                                      char *inargs) {
  if (exact)
    return find_opcode_exact(csound, opname, outargs, inargs);
  else
    return find_opcode_new(csound, opname, outargs, inargs);
}

static const CSOUND cenviron_ = {
    /* attributes  */
    csoundGetNchnls,
    csoundGetNchnlsInput,
    csoundGet0dBFS,
    csoundGetA4,
    csoundGetTieFlag,
    csoundGetReinitFlag,
    csoundGetInstrumentList,
    csoundGetStrsetsMax,
    csoundGetStrsets,
    csoundGetHostData,
    csoundGetCurrentTimeSamples,
    csoundGetInputBufferSize,
    csoundGetOutputBufferSize,
    csoundGetDebug,
    csoundGetSizeOfMYFLT,
    csoundGetParams,
    csoundGetEnv,
    csoundSystemSr,
    /* channels */
    csoundGetChannelPtr,
    csoundListChannels,
    /* events and performance */
    csoundYield,
    insert_score_event,
    csoundGetScoreOffsetSeconds,
    csoundSetScoreOffsetSeconds,
    csoundRewindScore,
    csoundInputMessageInternal,
    csoundReadScoreInternal,
    /* message printout */
    csoundMessage,
    csoundMessageS,
    csoundMessageV,
    csoundGetMessageLevel,
    csoundSetMessageLevel,
    csoundSetMessageCallback,
    /* arguments to opcodes and types*/
    get_arg_string,
    strarg2insno,
    strarg2name,
    GetType,
    csoundGetTypePool,
    csoundAddVariableType,
    /* memory allocation */
    csoundAuxAlloc,
    csoundAuxAllocAsync,
    mmalloc,
    mcalloc,
    mrealloc,
    cs_strdup,
    mfree,
    /* function tables */
    hfgens,
    csoundFTAlloc,
    csoundFTDelete,
    csoundFTFind,
    csoundGetNamedGens,
    /* global and config variable manipulation */
    csoundCreateGlobalVariable,
    csoundQueryGlobalVariable,
    csoundQueryGlobalVariableNoCheck,
    csoundDestroyGlobalVariable,
    csoundCreateConfigurationVariable,
    csoundSetConfigurationVariable,
    csoundParseConfigurationVariable,
    csoundQueryConfigurationVariable,
    csoundListConfigurationVariables,
    csoundDeleteConfigurationVariable,
    csoundCfgErrorCodeToString,
    /* FFT support */
    csoundRealFFT2Setup,
    csoundRealFFT2,
    csoundGetInverseRealFFTScale,
    csoundComplexFFT,
    csoundInverseComplexFFT,
    csoundGetInverseComplexFFTScale,
    csoundRealFFTMult,
    csoundDCTSetup,
    csoundDCT,
    /* LPC support */
    csoundAutoCorrelation,
    csoundLPsetup,
    csoundLPfree,
    csoundLPred,
    csoundLPCeps,
    csoundCepsLP,
    csoundLPrms,
    /* PVOC-EX system */
    pvoc_createfile,
    pvoc_openfile,
    pvoc_closefile,
    pvoc_putframes,
    pvoc_getframes,
    pvoc_framecount,
    pvoc_fseek,
    pvoc_errorstr,
    PVOCEX_LoadFile,
    /* error messages */
    csoundDie,
    csoundInitError,
    csoundPerfError,
    fterror,
    csoundWarning,
    csoundDebugMsg,
    csoundLongJmp,
    csoundErrorMsg,
    csoundErrMsgV,
    /* random numbers */
    csoundGetRandomSeedFromTime,
    csoundSeedRandMT,
    csoundRandMT,
    csoundRand31,
    RandSeed1,
    csoundGetRandSeed,
    /* threads and locks */
    csoundCreateThread,
    csoundJoinThread,
    csoundCreateThreadLock,
    csoundDestroyThreadLock,
    csoundWaitThreadLock,
    csoundNotifyThreadLock,
    csoundWaitThreadLockNoTimeout,
    csoundCreateMutex,
    csoundLockMutexNoWait,
    csoundLockMutex,
    csoundUnlockMutex,
    csoundDestroyMutex,
    csoundCreateBarrier,
    csoundDestroyBarrier,
    csoundWaitBarrier,
    csoundGetCurrentThreadId,
    csoundSleep,
    csoundInitTimerStruct,
    csoundGetRealTime,
    csoundGetCPUTime,
    /* circular buffer */
    csoundCreateCircularBuffer,
    csoundReadCircularBuffer,
    csoundWriteCircularBuffer,
    csoundPeekCircularBuffer,
    csoundFlushCircularBuffer,
    csoundDestroyCircularBuffer,
    /* File access */
    csoundFindInputFile,
    csoundFindOutputFile,
    csoundFileOpenWithType,
    csoundNotifyFileOpened,
    csoundFileClose,
    csoundFileError,
    csoundFileOpenWithType_Async,
    csoundReadAsync,
    csoundWriteAsync,
    csoundFSeekAsync,
    rewriteheader,
    csoundLoadSoundFile,
    ldmemfile2withCB,
    fdrecord,
    csound_fd_close,
    csoundCreateFileHandle,
    csoundGetFileName,
    type2csfiletype,
    sftype2csfiletype,
    type2string,
    getstrformat,
    sfsampsize,
    /* sndfile interface */
    sndfileOpen,
    sndfileOpenFd,
    sndfileClose,
    sndfileWrite,
    sndfileRead,
    sndfileWriteSamples,
    sndfileReadSamples,
    sndfileSeek,
    sndfileSetString,
    sndfileStrError,
    sndfileCommand,
    /* generic callbacks */
    csoundRegisterKeyboardCallback,
    csoundRemoveKeyboardCallback,
    csoundRegisterResetCallback,
    /* hash table funcs */
    cs_hash_table_create,
    cs_hash_table_get,
    cs_hash_table_put,
    cs_hash_table_remove,
    cs_hash_table_free,
    cs_hash_table_get_key,
    cs_hash_table_keys,
    cs_hash_table_values,
    /* opcodes and instruments  */
    csoundAppendOpcode,
    csoundAppendOpcodes,
    csoundFindOpcode,
    /* RT audio IO and callbacks */
    csoundSetPlayopenCallback,
    csoundSetRtplayCallback,
    csoundSetRecopenCallback,
    csoundSetRtrecordCallback,
    csoundSetRtcloseCallback,
    csoundSetAudioDeviceListCallback,
    csoundGetRtRecordUserData,
    csoundGetRtPlayUserData,
    csoundGetDitherMode,
    /* MIDI and callbacks */
    csoundSetExternalMidiInOpenCallback,
    csoundSetExternalMidiReadCallback,
    csoundSetExternalMidiInCloseCallback,
    csoundSetExternalMidiOutOpenCallback,
    csoundSetExternalMidiWriteCallback,
    csoundSetExternalMidiOutCloseCallback,
    csoundSetExternalMidiErrorStringCallback,
    csoundSetMIDIDeviceListCallback,
    module_list_add,
    /* displays & graphs */
    dispset,
    display,
    dispexit,
    dispinit,
    csoundSetIsGraphable,
    csoundSetMakeGraphCallback,
    csoundSetDrawGraphCallback,
    csoundSetKillGraphCallback,
    csoundSetExitGraphCallback,
    /* miscellaneous */
    csoundGetCsoundUtility,
    csoundPow2,
    csoundLocalizeString,
    cs_strtod,
    cs_sprintf,
    cs_sscanf,
    /* space for API expansion */
    {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
     NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
     NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
     NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
    /* ------- private data (not to be used by hosts or externals) ------- */
    /* callback function pointers */
 /* callback function pointers */
  (SUBR) NULL,    /*  first_callback_     */
  (channelCallback_t) NULL,
  (channelCallback_t) NULL,
  csoundDefaultMessageCallback,
  (int32_t (*)(CSOUND *)) NULL,
  (void (*)(CSOUND *, WINDAT *, const char *)) NULL, /* was: MakeAscii,*/
  (void (*)(CSOUND *, WINDAT *windat)) NULL, /* was: DrawAscii,*/
  (void (*)(CSOUND *, WINDAT *windat)) NULL, /* was: KillAscii,*/
  (int32_t (*)(CSOUND *)) NULL, /* was: defaultCsoundExitGraph, */
  defaultCsoundYield,
  cscore_,        /*  cscoreCallback_     */
  (void*(*)(CSOUND*, const char*, int32_t,  void*)) NULL,/* OpenSoundFileCallback_ */
  (FILE*(*)(CSOUND*, const char*, const char*)) NULL, /* OpenFileCallback_ */
  (void(*)(CSOUND*, const char*, int32_t,  int32_t,  int32_t)) NULL, /* FileOpenCallback_ */
  (SUBR) NULL,    /*  last_callback_      */
  /* these are not saved on RESET */
  playopen_dummy,
  rtplay_dummy,
  recopen_dummy,
  rtrecord_dummy,
  rtclose_dummy,
  audio_dev_list_dummy,
  midi_dev_list_dummy,
  csoundDoCallback_,  /*  doCsoundCallback    */
  defaultCsoundYield, /* csoundInternalYieldCallback_*/
  kperf_nodebug,  /* current kperf function - nodebug by default */
  (void (*)(CSOUND *csound, int32_t attr, const char *str)) NULL,/* message string callback */
  (void (*)(CSOUND *)) NULL,                      /*  spinrecv    */
  (void (*)(CSOUND *)) NULL,                      /*  spoutran    */
  (int32_t (*)(CSOUND *, MYFLT *, int32_t)) NULL,         /*  audrecv     */
  (void (*)(CSOUND *, const MYFLT *, int32_t)) NULL,  /*  audtran     */
  NULL,           /*  hostdata            */
  NULL, NULL,     /*  orchname, scorename */
  NULL, NULL,     /*  orchstr, *scorestr  */
  (OPDS*) NULL,   /*  ids                 */
  { (CS_VAR_POOL*)NULL,
    (CS_HASH_TABLE *) NULL,
    (CS_HASH_TABLE *) NULL,
    -1,
    (INSTRTXT**)NULL,
    { NULL,
      {
        0,0,
        NULL, NULL, NULL, NULL,
        0,0,
        NULL,
        0},
      0,0,0,
      //0,
      NULL,
      0,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      0,
      0,
      0,
      0,
      FL(0.0),
      NULL,
      NULL,
      0,
      0,
      0
    },
    NULL,
    MAXINSNO,     /* engineState          */
  },
  (INSTRTXT *) NULL, /* instr0  */
  (INSTRTXT**)NULL,  /* dead_instr_pool */
  0,                /* dead_instr_no */
  (TYPE_POOL*)NULL,
  DFLT_KSMPS,     /*  ksmps               */
  DFLT_NCHNLS,    /*  nchnls              */
  -1,             /*  inchns              */
  0L,             /*  kcounter            */
  0L,             /*  global_kcounter     */
  DFLT_SR,        /*  esr                 */
  DFLT_KR,        /*  ekr                 */
  0l,             /*  curTime             */
  0l,             /*  curTime_inc         */
  0.0,            /*  timeOffs            */
  0.0,            /*  beatOffs            */
  0.0,            /*  curBeat             */
  0.0,            /*  curBeat_inc         */
  0L,             /*  beatTime            */
  (EVTBLK*) NULL, /*  currevent           */
  (INSDS*) NULL,  /*  curip               */
  FL(0.0),        /*  cpu_power_busy      */
  (char*) NULL,   /*  xfilename           */
  1,              /*  peakchunks          */
  0,              /*  keep_tmp            */
  (CS_HASH_TABLE*)NULL, /* Opcode hash table */
  0,              /*  nrecs               */
  NULL,           /*  Linepipe            */
  0,              /*  Linefd              */
  NULL,           /*  csoundCallbacks_    */
  (FILE*)NULL,    /*  scfp                */
  (CORFIL*)NULL,  /*  scstr               */
  NULL,           /*  oscfp               */
  { FL(0.0) },    /*  maxamp              */
  { FL(0.0) },    /*  smaxamp             */
  { FL(0.0) },    /*  omaxamp             */
  {0}, {0}, {0},  /*  maxpos, smaxpos, omaxpos */
  NULL, NULL,     /*  scorein, scoreout   */
  NULL,           /*  argoffspace         */
  NULL,           /*  frstoff             */
  0,              /*  randSeed1           */
  0,              /*  randSeed2           */
  NULL,           /*  csRandState         */
  NULL,           /*  csRtClock           */
  // 16384,            /*  strVarMaxLen        */
  0,              /*  strsmax             */
  (char**) NULL,  /*  strsets             */
  NULL,           /*  spin                */
  NULL,           /*  spout               */
  NULL,           /*  spout_tmp               */
  0,              /*  nspin               */
  0,              /*  nspout              */
  NULL,           /*  auxspin             */
  (OPARMS*) NULL, /*  oparms              */
  { NULL },       /*  m_chnbp             */
  0,              /*   dither_output      */
  FL(0.0),        /*  onedsr              */
  FL(0.0),        /*  sicvt               */
  FL(-1.0),       /*  tpidsr              */
  FL(-1.0),       /*  pidsr               */
  FL(-1.0),       /*  mpidsr              */
  FL(-1.0),       /*  mtpdsr              */
  FL(0.0),        /*  onedksmps           */
  FL(0.0),        /*  onedkr              */
  FL(0.0),        /*  kicvt               */ 
  0,              /*  reinitflag          */
  0,              /*  tieflag             */
  DFLT_DBFS,      /*  e0dbfs              */
  FL(1.0) / DFLT_DBFS, /* dbfs_to_float ( = 1.0 / e0dbfs) */
  440.0,               /* A4 base frequency */
  NULL,           /*  rtRecord_userdata   */
  NULL,           /*  rtPlay_userdata     */
#if defined(MSVC) ||defined(__POWERPC__) || defined(MACOSX)
  {0},
#elif defined(LINUX)
  {{{0}}},        /*  exitjmp of type jmp_buf */
#else 
  {0},  
#endif 
  NULL,           /*  frstbp              */
  0,              /*  sectcnt             */
  0, 0, 0,        /*  inerrcnt, synterrcnt, perferrcnt */
  /* {NULL}, */   /*  instxtanchor  in engineState */
  {   /*  actanchor           */
    NULL,
    NULL,
    NULL,
    NULL, /*nxtdd*/
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    0,
    NULL,
    NULL,
    0,
    NULL,
    0,
    0,
    0,
    0,
    0,
    0.0,
    0.0,
    //    NULL,
    NULL,
    0,
    FL(0.0),  /* esr */
    FL(0.0),
    FL(0.0),
    FL(0.0),
    0,       /* in_cvt */
    0,       /* out_cvt */
    0,
    FL(0.0),
    FL(0.0), FL(0.0), FL(0.0),
    NULL,
    {FL(0.0), FL(0.0), FL(0.0), FL(0.0)},
    NULL,NULL,
    NULL,
    0,
    0,
    0,
    NULL,
    NULL,
    0,
    0,
    0,
    FL(0.0),
    NULL,
    NULL,
    0,  /* link flag */
    0,  /* instance id */
    {NULL, FL(0.0)},
    {NULL, FL(0.0)},
    {NULL, FL(0.0)},
    {NULL, FL(0.0)}  
  },
  {0L },          /*  rngcnt              */
  0, 0,           /*  rngflg, multichan   */
  NULL,           /*  evtFuncChain        */
  NULL,           /*  OrcTrigEvts         */
  NULL,           /*  freeEvtNodes        */
  1,              /*  csoundIsScorePending_ */
  0,              /*  advanceCnt          */
  0,              /*  initonly            */
  0,              /*  evt_poll_cnt        */
  0,              /*  evt_poll_maxcnt     */
  0, 0, 0,        /*  Mforcdecs, Mxtroffs, MTrkend */
  NULL,           /*  opcodeInfo  */
  NULL,           /*  flist               */
  0,              /*  maxfnum             */
  NULL,           /*  gensub              */
  GENMAX+1,       /*  genmax              */
  NULL,           /*  namedGlobals        */
  NULL,           /*  cfgVariableDB       */
  FL(0.0), FL(0.0), FL(0.0),  /*  prvbt, curbt, nxtbt */
  FL(0.0), FL(0.0),       /*  curp2, nxtim        */
  0,              /*  cyclesRemaining     */
  { 0, NULL, NULL, 0, '\0', 0, FL(0.0),
    FL(0.0), { FL(0.0) }, {NULL}},   /*  evt */
  NULL,           /*  memalloc_db         */
  (MGLOBAL*) NULL, /* midiGlobals         */
  NULL,           /*  envVarDB            */
  (MEMFIL*) NULL, /*  memfiles            */
  NULL,           /*  pvx_memfiles        */
  0,              /*  FFT_max_size        */
  NULL,           /*  FFT_table_1         */
  NULL,           /*  FFT_table_2         */
  NULL, NULL, NULL, /* tseg, tpsave, unused */
  (MYFLT*) NULL,  /*  gbloffbas           */
  NULL,           /* file_io_thread    */
  0,              /* file_io_start   */
  NULL,           /* file_io_threadlock */
  0,              /* realtime_audio_flag */
  NULL,           /* init pass thread */
  0,              /* init pass loop  */
  NULL,           /* init pass threadlock */
  NULL,           /* API_lock */
  SPINLOCK_INIT, SPINLOCK_INIT, /* spinlocks */
  SPINLOCK_INIT, SPINLOCK_INIT, /* spinlocks */
  NULL, NULL,             /* Delayed messages */
  {
    NULL, NULL, NULL, NULL, /* bp, prvibp, sp, nx */
    0, 0, 0, 0,   /*  op warpin linpos lincnt */
    -FL(1.0), FL(0.0), FL(1.0), /* prvp2 clock_base warp_factor */
    NULL,         /*  curmem              */
    NULL,         /*  memend              */
    NULL,         /*  macros              */
    -1,           /*  next_name           */
    NULL, NULL,   /*  inputs, str         */
    0,0,0,        /*  input_size, input_cnt, pop */
    1,            /*  ingappop            */
    -1,           /*  linepos             */
    {{NULL, 0, 0}}, /* names        */
    {""},         /*  repeat_name_n[RPTDEPTH][NAMELEN] */
    {0},          /*  repeat_cnt_n[RPTDEPTH] */
    {0},          /*  repeat_point_n[RPTDEPTH] */
    1, {NULL}, 0, /*  repeat_inc_n,repeat_mm_n repeat_index */
    "",          /*  repeat_name[NAMELEN] */
    0,0,1,        /*  repeat_cnt, repeat_point32_t,  repeat_inc */
    NULL,         /*  repeat_mm */
    0
  },
  {
    NULL,
    NULL, NULL, NULL, /* orcname, sconame, midname */
    0, 0           /* midiSet, csdlinecount */
  },
  {
    NULL, NULL,   /* Linep, Linebufend    */
    0,            /* stdmode              */
    {
      0, NULL, NULL, 0, 0, 0, FL(0.0), FL(0.0), { FL(0.0) },
      {NULL},
    },            /* EVTBLK  prve         */
    NULL,        /* Linebuf              */
    0,            /* linebufsiz */
    NULL, NULL,
    0
  },
  {
    {0,0}, {0,0},  /* srngcnt, orngcnt    */
    0, 0, 0, 0, 0, /* srngflg, sectno, lplayed, segamps, sormsg */
    NULL, NULL,    /* ep, epend           */
    NULL           /* lsect               */
  },
  //NULL,           /*  musmonGlobals       */
  {
    NULL,         /*  outfile             */
    NULL,         /*  infile              */
    NULL,         /*  sfoutname;          */
    NULL,         /*  inbuf               */
    NULL,         /*  outbuf              */
    NULL,         /*  outbufp             */
    0,            /*  inbufrem            */
    0,            /*  outbufrem           */
    0,0,          /*  inbufsiz,  outbufsiz */
    0,            /*  isfopen             */
    0,            /*  osfopen             */
    0,0,          /*  pipdevin, pipdevout */
    1U,           /*  nframes             */
    NULL, NULL,   /*  pin, pout           */
    0,            /*dither                */
  },
  0,              /*  warped              */
  0,              /*  sstrlen             */
  (char*) NULL,   /*  sstrbuf             */
  1,              /*  enableMsgAttr       */
  0,              /*  sampsNeeded         */
  FL(0.0),        /*  csoundScoreOffsetSeconds_   */
  -1,             /*  inChar_             */
  0,              /*  isGraphable_        */
  0,              /*  delayr_stack_depth  */
  NULL,           /*  first_delayr        */
  NULL,           /*  last_delayr         */
  { 0L, 0L, 0L, 0L, 0L, 0L },     /*  revlpsiz    */
  0L,             /*  revlpsum            */
  0.5,            /*  rndfrac             */
  NULL,           /*  logbase2            */
  NULL, NULL,     /*  omacros, smacros    */
  NULL,           /*  namedgen            */
  NULL,           /*  open_files          */
  NULL,           /*  searchPathCache     */
  NULL,           /*  sndmemfiles         */
  NULL,           /*  reset_list          */
  NULL,           /*  pvFileTable         */
  0,              /*  pvNumFiles          */
  0,              /*  pvErrorCode         */
  //    NULL,           /*  pluginOpcodeFiles   */
  0,              /*  enableHostImplementedAudioIO  */
  0,              /* MIDI IO */
  0,              /*  hostRequestedBufferSize       */
  0,              /*  engineStatus         */
  0,              /*  stdin_assign_flg    */
  0,              /*  stdout_assign_flg   */
  0,              /*  orcname_mode        */
  0,              /*  use_only_orchfile   */
  NULL,           /*  csmodule_db         */
  (char*) NULL,   /*  dl_opcodes_oplibs   */
  (char*) NULL,   /*  SF_csd_licence      */
  (char*) NULL,   /*  SF_id_title         */
  (char*) NULL,   /*  SF_id_copyright     */
  -1,             /*  SF_id_scopyright    */
  (char*) NULL,   /*  SF_id_software      */
  (char*) NULL,   /*  SF_id_artist        */
  (char*) NULL,   /*  SF_id_comment       */
  (char*) NULL,   /*  SF_id_date          */
  NULL,           /*  utility_db          */
  (int16*) NULL,  /*  isintab             */
  NULL,           /*  lprdaddr            */
  0,              /*  currentLPCSlot      */
  0,              /*  max_lpc_slot        */
  NULL,           /*  chn_db              */
  1,              /*  opcodedirWasOK      */
  0,              /*  disable_csd_options */
  { 0, { 0U } },  /*  randState_          */
  0,              /*  performState        */
  1000,           /*  ugens4_rand_16      */
  1000,           /*  ugens4_rand_15      */
  NULL,           /*  schedule_kicked     */
  (MYFLT*) NULL,  /*  disprep_fftcoefs    */
  NULL,           /*  winEPS_globals      */
  {               /*  oparms_             */
    0,            /*    odebug            */
    0, 1, 0,   /*    sfread, ...       */
    0, 0, 0, 0,   /*    inbufsamps, ...   */
    0,            /*    sfsampsize        */
    1,            /*    displays          */
    1, 0, 135,    /*    graphsoff ...     */
    0, 0,         /*    Beatmode, ...     */
    0, 0,         /*    usingcscore, ...  */
    0, 0, 0, 0,   /*    RTevents, ...     */
    0, 0,         /*    ringbell, ...     */
    0, 0, 0,      /*    rewrt_hdr, ...    */
    0.0,          /*    cmdTempo          */
    0.0f, 0.0f,   /*    sr_override ...   */
    0, 0,     /*    nchnls_override ...   */
    (char*) NULL, (char*) NULL,   /* filenames */
    (char*) NULL, (char*) NULL, (char*) NULL,
    (char*) NULL, (char*) NULL,
    0,            /*    midiKey           */
    0,            /*    midiKeyCps        */
    0,            /*    midiKeyOct        */
    0,            /*    midiKeyPch        */
    0,            /*    midiVelocity      */
    0,            /*    midiVelocityAmp   */
    0,            /*    noDefaultPaths    */
    1,            /*    numThreads        */
    0,            /*    syntaxCheckOnly   */
    1,            /*    useCsdLineCounts  */
    0,            /*    samp acc   */
    0,            /*    realtime  */
    0.0,          /*    0dbfs override */
    0,            /*    no exit on compile error */
    0.4,          /*    vbr quality  */
    0,            /*    ksmps_override */
    0,             /*    fft_lib */
    0,             /*    echo */
    0.0,           /*   limiter */
    DFLT_SR, DFLT_KR,  /* defaults */
    0,             /* mp3 mode */
    0              /* instr redefinition flag */ 
  },
  {0, 0, {0}}, /* REMOT_BUF */
  NULL,           /* remoteGlobals        */
  0, 0,           /* nchanof, nchanif     */
  NULL, NULL,     /* chanif, chanof       */
  0,              /* multiThreadedComplete */
  NULL,           /* multiThreadedThreadInfo */
  NULL,           /* multiThreadedDag */
  NULL,           /* barrier1 */
  NULL,           /* barrier2 */
  /* statics from cs_par_orc_semantic_analysis */
  NULL,           /* instCurr */
  NULL,           /* instRoot */
  0,              /* inInstr */
  /* new dag model statics */
  1,              /* dag_changed */
  0,              /* dag_num_active */
  NULL,           /* dag_task_map */
  NULL,           /* dag_task_status */
  NULL,           /* dag_task_watch */
  NULL,           /* dag_wlmm */
  NULL,           /* dag_task_dep */
  100,            /* dag_task_max_size */
  0,              /* tempStatus */
  1,              /* orcLineOffset */
  0,              /* scoLineOffset */
  NULL,           /* csdname */
  0,              /*  parserNamedInstrFlag */
  0,              /*  tran_nchnlsi */
  0,              /* Count of score strings */
  0,              /* length of current strings space */
  NULL,           /* sinetable */
  16384,          /* sinesize */
  NULL,           /* unused *** pow2 table */
  NULL,           /* cps conv table */
  NULL,           /* output of preprocessor */
  NULL,           /* output of preprocessor */
  {NULL},         /* filedir */
  NULL,           /* message buffer struct */
  0,              /* jumpset */
  0,              /* info_message_request */
  0,              /* modules loaded */
  -1,             /* audio system sr */
  0,              /* csdebug_data */
  0,              /* which score parser */
  0,              /* print_version */
  1,              /* inZero */
  NULL,           /* msg_queue */
  0,              /* msg_queue_wget */
  0,              /* msg_queue_wput */
  0,              /* msg_queue_rstart */
  0,              /* msg_queue_items */
  127,            /* aftouch */
  NULL,           /* directory for corfiles */
  NULL,           /* alloc_queue */
  0,              /* alloc_queue_items */
  0,              /* alloc_queue_wp */
  SPINLOCK_INIT,  /* alloc_spinlock */
  NULL,           /* init_event */
  NULL,           /* message_string */
  0,              /* message_string_queue_items */
  0,              /* message_string_queue_wp */
  NULL,           /* message_string_queue */
  0,              /* io_initialised */
  0,              /* options_checked */    
  NULL,           /* op */
  0,              /* mode */
  NULL,           /* opcodedir */
  NULL,           /* score_srt */
  {NULL, NULL, NULL, 0, 0, NULL}, /* osc_message_anchor */
  NULL,
  SPINLOCK_INIT,
  {                /* csound_util */
  csoundAddUtility,
  csoundRunUtility,
  csoundListUtilities,
  csoundSetUtilityDescription,
  csoundGetUtilityDescription,
  set_util_sr,
  set_util_nchnls,
  SAsndgetset,
  sndgetset,
  getsndin
  },
  0 /* instance count */    
};

void csound_aops_init_tables(CSOUND *cs);

typedef struct csInstance_s {
  CSOUND *csound;
  struct csInstance_s *nxt;
} csInstance_t;

/* initialisation state: */
/* 0: not done yet, 1: complete, 2: in progress, -1: failed */
static volatile int32_t init_done = 0;
/* chain of allocated Csound instances */
static volatile csInstance_t *instance_list = NULL;
/* non-zero if performance should be terminated now */
static volatile int32_t exitNow_ = 0;

#if !defined(WIN32)
static void destroy_all_instances(void) {
  volatile csInstance_t *p;

  csoundLock();
  init_done = -1; /* prevent the creation of any new instances */
  if (instance_list == NULL) {
    csoundUnLock();
    return;
  }
  csoundUnLock();
  csoundSleep(250);
  while (1) {
    csoundLock();
    p = instance_list;
    csoundUnLock();
    if (p == NULL) {
      break;
    }
    csoundDestroy(p->csound);
  }
}
#endif

#if defined(ANDROID) ||                                                        \
    (!defined(LINUX) && !defined(SGI) && !defined(__HAIKU__) &&                \
     !defined(__BEOS__) && !defined(__MACH__) && !defined(__EMSCRIPTEN__))

static char *signal_to_string(int32_t sig) {
  switch (sig) {
#ifdef SIGHUP
  case SIGHUP:
    return "Hangup";
#endif
#ifdef SIGINT
  case SIGINT:
    return "Interrupt";
#endif
#ifdef SIGQUIT
  case SIGQUIT:
    return "Quit";
#endif
#ifdef SIGILL
  case SIGILL:
    return "Illegal instruction";
#endif
#ifdef SIGTRAP
  case SIGTRAP:
    return "Trace trap";
#endif
#ifdef SIGABRT
  case SIGABRT:
    return "Abort";
#endif
#ifdef SIGBUS
  case SIGBUS:
    return "BUS error";
#endif
#ifdef SIGFPE
  case SIGFPE:
    return "Floating-point exception";
#endif
#ifdef SIGUSR1
  case SIGUSR1:
    return "User-defined signal 1";
#endif
#ifdef SIGSEGV
  case SIGSEGV:
    return "Segmentation violation";
#endif
#ifdef SIGUSR2
  case SIGUSR2:
    return "User-defined signal 2";
#endif
#ifdef SIGPIPE
  case SIGPIPE:
    return "Broken pipe";
#endif
#ifdef SIGALRM
  case SIGALRM:
    return "Alarm clock";
#endif
#ifdef SIGTERM
  case SIGTERM:
    return "Termination";
#endif
#ifdef SIGSTKFLT
  case SIGSTKFLT:
    return "???";
#endif
#ifdef SIGCHLD
  case SIGCHLD:
    return "Child status has changed";
#endif
#ifdef SIGCONT
  case SIGCONT:
    return "Continue";
#endif
#ifdef SIGSTOP
  case SIGSTOP:
    return "Stop, unblockable";
#endif
#ifdef SIGTSTP
  case SIGTSTP:
    return "Keyboard stop";
#endif
#ifdef SIGTTIN
  case SIGTTIN:
    return "Background read from tty";
#endif
#ifdef SIGTTOU
  case SIGTTOU:
    return "Background write to tty";
#endif
#ifdef SIGURG
  case SIGURG:
    return "Urgent condition on socket ";
#endif
#ifdef SIGXCPU
  case SIGXCPU:
    return "CPU limit exceeded";
#endif
#ifdef SIGXFSZ
  case SIGXFSZ:
    return "File size limit exceeded ";
#endif
#ifdef SIGVTALRM
  case SIGVTALRM:
    return "Virtual alarm clock ";
#endif
#ifdef SIGPROF
  case SIGPROF:
    return "Profiling alarm clock";
#endif
#ifdef SIGWINCH
  case SIGWINCH:
    return "Window size change ";
#endif
#ifdef SIGIO
  case SIGIO:
    return "I/O now possible";
#endif
#ifdef SIGPWR
  case SIGPWR:
    return "Power failure restart";
#endif
  default:
    return "???";
  }
}

#ifdef ANDROID
static void psignal_(int sig, char *str) {
  fprintf(stderr, "%s: %s\n", str, signal_to_string(sig));
}
#else
#if !defined(__CYGWIN__)
void psignal(int sig, const char *str) {
  fprintf(stderr, "%s: %s\n", str, signal_to_string(sig));
}
#endif
#endif
#elif defined(__BEOS__)
static void psignal_(int sig, char *str) {
  fprintf(stderr, "%s: %s\n", str, strsignal(sig));
}
#endif

static void signal_handler(int sig) {
#if defined(HAVE_EXECINFO) && !defined(ANDROID)
#include <execinfo.h>

  {
    int32_t j, nptrs;
#define SIZE 100
    void *buffer[SIZE];
    char **strings;

    nptrs = backtrace(buffer, SIZE);
    printf("backtrace() returned %d addresses\n", nptrs);

    /* The call backtrace_symbols_fd(buffer, nptrs, STDOUT_FILENO)
       would produce similar output to the following: */

    strings = backtrace_symbols(buffer, nptrs);
    if (UNLIKELY(strings == NULL)) {
      perror("backtrace_symbols");
      exit(EXIT_FAILURE);
    }

    for (j = 0; j < nptrs; j++)
      printf("%s\n", strings[j]);

    free(strings);
  }
#endif

#if defined(SIGPIPE)
  if (sig == (int32_t)SIGPIPE) {
#ifdef ANDROID
    psignal_(sig, "Csound ignoring SIGPIPE");
#elif !defined(__wasm__)
    psignal(sig, "Csound ignoring SIGPIPE");
#endif
    return;
  }
#endif
#ifdef ANDROID
  psignal_(sig, "Csound tidy up");
#elif !defined(__wasm__)
  psignal(sig, "Csound tidy up");
#endif
  if ((sig == (int32_t)SIGINT || sig == (int32_t)SIGTERM) && !exitNow_) {
    exitNow_ = -1;
    return;
  }
  exit(1);
}

static const int32_t sigs[] = {
#if defined(LINUX) || defined(SGI) || defined(sol) || defined(__MACH__)
    SIGHUP, SIGINT, SIGQUIT, SIGILL,  SIGTRAP, SIGABRT, SIGIOT,
    SIGBUS, SIGFPE, SIGSEGV, SIGPIPE, SIGTERM, SIGXCPU, SIGXFSZ,
#elif defined(WIN32)
    SIGINT, SIGILL, SIGABRT, SIGFPE, SIGSEGV, SIGTERM,
#elif defined(__EMX__)
    SIGHUP, SIGINT,  SIGQUIT, SIGILL,  SIGTRAP, SIGABRT, SIGBUS,
    SIGFPE, SIGUSR1, SIGSEGV, SIGUSR2, SIGPIPE, SIGTERM, SIGCHLD,
#endif
    -1};

static void install_signal_handler(void) {
  uint32_t i;
  for (i = 0; sigs[i] >= 0; i++) {
    signal(sigs[i], signal_handler);
  }
}

static int32_t getTimeResolution(void);

PUBLIC int32_t csoundInitialize(int32_t flags) {
  int32_t n;

  do {
    csoundLock();
    n = init_done;
    switch (n) {
    case 2:
      csoundUnLock();
      csoundSleep(1);
    case 0:
      break;
    default:
      csoundUnLock();
      return n;
    }
  } while (n);
  init_done = 2;
  csoundUnLock();
  if (getTimeResolution() != 0) {
    csoundLock();
    init_done = -1;
    csoundUnLock();
    return -1;
  }
  if (!(flags & CSOUNDINIT_NO_SIGNAL_HANDLER)) {
    install_signal_handler();
  }
#if !defined(WIN32)
  if (!(flags & CSOUNDINIT_NO_ATEXIT))
    atexit(destroy_all_instances);
#endif
  csoundLock();
  init_done = 1;
  csoundUnLock();
  return 0;
}

PUBLIC CSOUND *csoundCreate(void *hostdata, const char *opcodedir) {
  CSOUND *csound;
  csInstance_t *p;
  _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);

  if (init_done != 1) {
    if (csoundInitialize(0) < 0)
      return NULL;
  }
  csound = (CSOUND *)malloc(sizeof(CSOUND));
  if (UNLIKELY(csound == NULL))
    return NULL;
  memcpy(csound, &cenviron_, sizeof(CSOUND));
  init_getstring(csound);
  csound->oparms = &(csound->oparms_);
  csound->hostdata = hostdata;
  p = (csInstance_t *)malloc(sizeof(csInstance_t));
  if (UNLIKELY(p == NULL)) {
    free(csound);
    return NULL;
  }
  csoundLock();
  p->csound = csound;
  p->nxt = (csInstance_t *)instance_list;
  instance_list = p;
  csoundUnLock();
  // no cs_strdup yet so we use strdup and
  // free it later in csoundDestroy
  if (opcodedir != NULL)
    csound->opcodedir = strdup(opcodedir);
  csoundReset(csound);
  csound->API_lock = csoundCreateMutex(1);
  allocate_message_queue(csound);
  /* NB: as suggested by F Pinot, keep the
     address of the pointer to CSOUND inside
     the struct, so it can be cleared later */
  // csound->self = &csound;
  return csound;
}

/* dummy real time MIDI functions */
int32_t DummyMidiInOpen(CSOUND *csound, void **userData, const char *devName);
int32_t DummyMidiRead(CSOUND *csound, void *userData, unsigned char *buf,
                      int32_t nbytes);
int32_t DummyMidiOutOpen(CSOUND *csound, void **userData, const char *devName);
int32_t DummyMidiWrite(CSOUND *csound, void *userData, const unsigned char *buf,
                       int32_t nbytes);
/* random.c */
extern void csound_init_rand(CSOUND *);

typedef struct CsoundCallbackEntry_s CsoundCallbackEntry_t;

struct CsoundCallbackEntry_s {
  uint32_t typeMask;
  CsoundCallbackEntry_t *nxt;
  void *userData;
  int32_t (*func)(void *, void *, uint32_t);
};

PUBLIC void csoundDestroy(CSOUND *csound) {
  csInstance_t *p, *prv = NULL;

  csoundLock();
  p = (csInstance_t *)instance_list;
  while (p != NULL && p->csound != csound) {
    prv = p;
    p = p->nxt;
  }
  if (p == NULL) {
    csoundUnLock();
    return;
  }
  if (prv == NULL)
    instance_list = p->nxt;
  else
    prv->nxt = p->nxt;
  csoundUnLock();
  free(p);

  reset(csound);

  if (csound->csoundCallbacks_ != NULL) {
    CsoundCallbackEntry_t *pp, *nxt;
    pp = (CsoundCallbackEntry_t *)csound->csoundCallbacks_;
    do {
      nxt = pp->nxt;
      free((void *)pp);
      pp = nxt;
    } while (pp != (CsoundCallbackEntry_t *)NULL);
  }
  if (csound->API_lock != NULL) {
    // csoundLockMutex(csound->API_lock);
    csoundDestroyMutex(csound->API_lock);
  }
  // free opcodedir
  free(csound->opcodedir);
  /* clear the pointer */
  // *(csound->self) = NULL;
  free((void *)csound);
}

PUBLIC int32_t csoundGetVersion(void) {
  return (int32_t)(CS_VERSION * 1000 + CS_SUBVER * 10 + CS_PATCHLEVEL);
}

PUBLIC void *csoundGetHostData(CSOUND *csound) {
  return csound->hostdata;
}

PUBLIC void csoundSetHostData(CSOUND *csound, void *hostData) {
  csound->hostdata = hostData;
}

/*
 * PERFORMANCE
 */

extern int32_t sensevents(CSOUND *);

#ifdef PARCS
/**
 * perform currently active instrs for one kperiod
 *      & send audio result to output buffer
 * returns non-zero if this kperiod was skipped
 */
static int32_t getThreadIndex(CSOUND *csound, void *threadId) {
  int32_t index = 0;
  THREADINFO *current = csound->multiThreadedThreadInfo;

  if (current == NULL) {
    return -1;
  }

  while (current != NULL) {
#ifdef HAVE_PTHREAD
    if (pthread_equal(*(pthread_t *)threadId, *(pthread_t *)current->threadId))
#elif defined(WIN32)
    DWORD *d = (DWORD *)threadId;
    if (*d == GetThreadId((HANDLE)current->threadId))
#else
    // FIXME - need to verify this works...
    if (threadId == current->threadId)
#endif
      return index;

    index++;
    current = current->next;
  }
  return -1;
}
#endif

#if 0
static int32_t getNumActive(INSDS *start, INSDS *end)
{
  INSDS *current = start;
  int32_t counter = 1;
  while (((current = current->nxtact) != NULL) && current != end) {
    counter++;
  }
  return counter;
}
#endif

inline void advanceINSDSPointer(INSDS ***start, int32_t num) {
  int32_t i;
  INSDS *s = **start;

  if (s == NULL)
    return;
  for (i = 0; i < num; i++) {
    s = s->nxtact;

    if (s == NULL) {
      **start = NULL;
      return;
    }
  }
  **start = s;
}

inline static void mix_out(MYFLT *out, MYFLT *in, uint32_t smps) {
  uint32_t i;
  for (i = 0; i < smps; i++)
    out[i] += in[i];
}

int32_t dag_get_task(CSOUND *csound, int32_t index, int32_t numThreads,
                     int32_t next_task);
int32_t dag_end_task(CSOUND *csound, int32_t task);
void dag_build(CSOUND *csound, INSDS *chain);
void dag_reinit(CSOUND *csound);

#ifdef PARCS
inline static int32_t nodePerf(CSOUND *csound, int32_t index,
                               int32_t numThreads) {
  INSDS *insds = NULL;
  OPDS *opstart = NULL;
  int32_t played_count = 0;
  int32_t which_task;
  INSDS **task_map = (INSDS **)csound->dag_task_map;
  double time_end;
#define INVALID (-1)
#define WAIT (-2)
  int32_t next_task = INVALID;
  IGN(index);

  while (1) {
    int32_t done;
    which_task = dag_get_task(csound, index, numThreads, next_task);
    // printf("******** Select task %d %d\n", which_task, index);
    if (which_task == WAIT)
      continue;
    if (which_task == INVALID)
      return played_count;
    /* VL: the validity of icurTime needs to be checked */
    time_end = (csound->ksmps + csound->icurTime) / csound->esr;
    insds = task_map[which_task];
    if (insds->offtim > 0 && time_end > insds->offtim) {
      /* this is the last cycle of performance */
      insds->ksmps_no_end = insds->no_end;
    }
#if defined(MSVC)
    done = InterlockedExchangeAdd(&insds->init_done, 0);
#elif defined(HAVE_ATOMIC_BUILTIN)
    done = __atomic_load_n((int32_t *)&insds->init_done, __ATOMIC_SEQ_CST);
#else
    done = insds->init_done;
#endif

    if (done) {
      opstart = (OPDS *)task_map[which_task];
      if (insds->ksmps == csound->ksmps) {
        insds->spin = csound->spin;
        insds->spout = csound->spout_tmp + index * csound->nspout;
        insds->kcounter = csound->kcounter;
        csound->mode = 2;
        while ((opstart = opstart->nxtp) != NULL) {
          /* In case of jumping need this repeat of opstart */
          opstart->insdshead->pds = opstart;
          csound->op = opstart->optext->t.opcod;
          (*opstart->perf)(csound, opstart); /* run each opcode */
          opstart = opstart->insdshead->pds;
        }
        csound->mode = 0;
      } else {
        int32_t i, n = csound->nspout, start = 0;
        int32_t lksmps = insds->ksmps;
        int32_t incr = csound->nchnls * lksmps;
        int32_t offset = insds->ksmps_offset;
        int32_t early = insds->ksmps_no_end;
        OPDS *opstart;
        insds->spin = csound->spin;
        insds->spout = csound->spout_tmp + index * csound->nspout;
        insds->kcounter = csound->kcounter * csound->ksmps;

        /* we have to deal with sample-accurate code
           whole CS_KSMPS blocks are offset here, the
           remainder is left to each opcode to deal with.
        */
        while (offset >= lksmps) {
          offset -= lksmps;
          start += csound->nchnls;
        }
        insds->ksmps_offset = offset;
        if (UNLIKELY(early)) {
          n -= (early * csound->nchnls);
          insds->ksmps_no_end = early % lksmps;
        }
        for (i = start; i < n;
             i += incr, insds->spin += incr, insds->spout += incr) {
          opstart = (OPDS *)insds;
          csound->mode = 2;
          while ((opstart = opstart->nxtp) != NULL) {
            opstart->insdshead->pds = opstart;
            csound->op = opstart->optext->t.opcod;
            (*opstart->perf)(csound, opstart); /* run each opcode */
            opstart = opstart->insdshead->pds;
          }
          csound->mode = 0;
          insds->kcounter++;
        }
      }
      insds->ksmps_offset = 0; /* reset sample-accuracy offset */
      insds->ksmps_no_end = 0; /* reset end of loop samples */
      played_count++;
    }
    // printf("******** finished task %d\n", which_task);
    next_task = dag_end_task(csound, which_task);
  }
  return played_count;
}
#endif // PARCS

#ifdef PARCS
unsigned long kperfThread(void *cs) {
  // INSDS *start;
  CSOUND *csound = (CSOUND *)cs;
  void *threadId;
  int32_t index;
  int32_t numThreads;
  _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);

  csound->WaitBarrier(csound->barrier2);

  threadId = csound->GetCurrentThreadID();
  index = getThreadIndex(csound, threadId);
  numThreads = csound->oparms->numThreads;
  // start = NULL;
  csound->Message(csound,
                  Str("Multithread performance:thread %d of "
                      "%d starting.\n"),
                  /* start ? start->insno : */
                  index + 1, numThreads);
  if (UNLIKELY(index < 0)) {
    csound->Die(csound, Str("Bad ThreadId"));
    return ULONG_MAX;
  }
  index++;

  while (1) {

    csound->WaitBarrier(csound->barrier1);

    // FIXME:PTHREAD_WORK - need to check if this is necessary and, if so,
    // use some other kind of locking mechanism as it isn't clear why a
    // global mutex would be necessary versus a per-CSOUND instance mutex
    /*csound_global_mutex_lock();*/
    if (csound->multiThreadedComplete == 1) {
      /*csound_global_mutex_unlock();*/
      free(threadId);
      return 0UL;
    }
    /*csound_global_mutex_unlock();*/

    nodePerf(csound, index, numThreads);

    csound->WaitBarrier(csound->barrier2);
  }
}
#endif

int32_t kperf_nodebug(CSOUND *csound) {
  INSDS *ip;
  int32_t lksmps = csound->ksmps;
  /* update orchestra time */
  csound->kcounter = ++(csound->global_kcounter);
  csound->icurTime += csound->ksmps;
  csound->curBeat += csound->curBeat_inc;

  /* call message_dequeue to run API calls */
  message_dequeue(csound);

  /* if skipping time on request by 'a' score statement: */
  if (UNLIKELY(UNLIKELY(csound->advanceCnt))) {
    csound->advanceCnt--;
    return 1;
  }
  /* if i-time only, return now */
  if (UNLIKELY(csound->initonly))
    return 1;
  /* PC GUI needs attention, but avoid excessively frequent */
  /* calls of csoundYield() */
  if (UNLIKELY(--(csound->evt_poll_cnt) < 0)) {
    csound->evt_poll_cnt = csound->evt_poll_maxcnt;
    if (UNLIKELY(!csoundYield(csound)))
      csound->LongJmp(csound, 1);
  }

  /* for one kcnt: */
  if (csound->oparms_.sfread) /*   if audio_infile open  */
    csound->spinrecv(csound); /*      fill the spin buf  */
  /* clear spout */
  memset(csound->spout, 0, csound->nspout * sizeof(MYFLT));
  memset(csound->spout_tmp, 0,
         sizeof(MYFLT) * csound->nspout * csound->oparms->numThreads);
  ip = csound->actanchor.nxtact;

  if (ip != NULL) {
    /* There are 2 partitions of work: 1st by inso,
       2nd by inso count / thread count. */
    if (csound->multiThreadedThreadInfo != NULL) {
#ifdef PARCS
      if (csound->dag_changed)
        dag_build(csound, ip);
      else
        dag_reinit(csound); /* set to initial state */

      /* process this partition */
      csound->WaitBarrier(csound->barrier1);

      (void)nodePerf(csound, 0, 1);

      /* wait until partition is complete */
      csound->WaitBarrier(csound->barrier2);

      // do the mixing of thread buffers
      {
        int32_t k;
        for (k = 1; k < csound->oparms->numThreads; k++)
          mix_out(csound->spout_tmp, csound->spout_tmp + k * csound->nspout,
                  csound->nspout);
      }
#endif
      csound->multiThreadedDag = NULL;
    } else {
      int32_t done;
      double time_end = (csound->ksmps + csound->icurTime) / csound->esr;

      while (ip != NULL) { /* for each instr active:  */
        INSDS *nxt = ip->nxtact;
        if (UNLIKELY(csound->oparms->sampleAccurate && ip->offtim > 0 &&
                     time_end > ip->offtim)) {
          /* this is the last cycle of performance */
          //   csound->Message(csound, "last cycle %d: %f %f %d\n",
          //       ip->insno, csound->icurTime/csound->esr,
          //          ip->offtim, ip->no_end);
          ip->ksmps_no_end = ip->no_end;
        }
        done = ATOMIC_GET(ip->init_done);
        if (done == 1) { /* if init-pass has been done */
          int32_t error = 0;
          OPDS *opstart = (OPDS *)ip;
          ip->spin = csound->spin;
          ip->spout = csound->spout_tmp;
          ip->kcounter = csound->kcounter;
          if (ip->ksmps == csound->ksmps) {
            csound->mode = 2;
            while (error == 0 && opstart != NULL &&
                   (opstart = opstart->nxtp) != NULL && ip->actflg) {
              opstart->insdshead->pds = opstart;
              csound->op = opstart->optext->t.opcod;
              error = (*opstart->perf)(csound, opstart); /* run each opcode */
              opstart = opstart->insdshead->pds;
            }
            csound->mode = 0;
          } else {
            int32_t error = 0;
            int32_t i, n = csound->nspout, start = 0;
            lksmps = ip->ksmps;
            int32_t incr = csound->nchnls * lksmps;
            int32_t offset = ip->ksmps_offset;
            int32_t early = ip->ksmps_no_end;
            OPDS *opstart;
            ip->kcounter = (csound->kcounter - 1) * csound->ksmps / lksmps;

            /* we have to deal with sample-accurate code
               whole CS_KSMPS blocks are offset here, the
               remainder is left to each opcode to deal with.
            */
            while (offset >= lksmps) {
              offset -= lksmps;
              start += csound->nchnls;
            }
            ip->ksmps_offset = offset;
            if (UNLIKELY(early)) {
              n -= (early * csound->nchnls);
              ip->ksmps_no_end = early % lksmps;
            }
            for (i = start; i < n;
                 i += incr, ip->spin += incr, ip->spout += incr) {
              ip->kcounter++;
              opstart = (OPDS *)ip;
              csound->mode = 2;
              while (error == 0 && (opstart = opstart->nxtp) != NULL &&
                     ip->actflg) {
                opstart->insdshead->pds = opstart;
                csound->op = opstart->optext->t.opcod;
                // csound->ids->optext->t.oentry->opname;
                error = (*opstart->perf)(csound, opstart); /* run each opcode */
                opstart = opstart->insdshead->pds;
              }
              csound->mode = 0;
            }
          }
        }
        ip->ksmps_offset = 0; /* reset sample-accuracy offset */
        ip->ksmps_no_end = 0; /* reset end of loop samples */
        if(nxt == NULL) {
          ip = ip->nxtact;
        /* VL 13.04.21 this allows for deletions to operate
           correctly on the active list at perf time.
           This allows for turnoff2 to work correctly
        */
        }
        else {
          ip = nxt;
          /* now check again if there is nothing nxt
             in the chain making sure turnoff also works  */
        }
      }
    }
  }
  csound->spoutran(csound); /* send to audio_out */
  return 0;
}

static inline void opcode_perf_debug(CSOUND *csound, csdebug_data_t *data,
                                     INSDS *ip) {
  OPDS *opstart = (OPDS *)ip;
  while ((opstart = opstart->nxtp) != NULL) {
    /* check if we have arrived at a line breakpoint */
    bkpt_node_t *bp_node = data->bkpt_anchor->next;
    if (data->debug_opcode_ptr) {
      opstart = data->debug_opcode_ptr;
      data->debug_opcode_ptr = NULL;
    }
    int32_t linenum = opstart->optext->t.linenum;
    while (bp_node) {
      if (bp_node->instr == ip->p1.value || (bp_node->instr == 0)) {
        if ((bp_node->line) == linenum) { /* line matches */
          if (bp_node->count < 2) { /* skip of 0 or 1 has the same effect */
            if (data->debug_opcode_ptr != opstart) { /* did we just stop here */
              data->debug_instr_ptr = ip;
              data->debug_opcode_ptr = opstart;
              data->status = CSDEBUG_STATUS_STOPPED;
              data->cur_bkpt = bp_node;
              csoundDebuggerBreakpointReached(csound);
              bp_node->count = bp_node->skip;
              return;
            } else {
              data->debug_opcode_ptr = NULL; /* if just stopped here-continue */
            }
          } else {
            bp_node->count--;
          }
        }
      }
      bp_node = bp_node->next;
    }
    opstart->insdshead->pds = opstart;
    csound->mode = 2;
    (*opstart->perf)(csound, opstart); /* run each opcode */
    opstart = opstart->insdshead->pds;
    csound->mode = 0;
  }
  mix_out(csound->spout, ip->spout, ip->ksmps * csound->nchnls);
}

static inline void process_debug_buffers(CSOUND *csound, csdebug_data_t *data) {
  bkpt_node_t *bkpt_node;
  while (csoundReadCircularBuffer(csound, data->bkpt_buffer, &bkpt_node, 1) ==
         1) {
    if (bkpt_node->mode == CSDEBUG_BKPT_CLEAR_ALL) {
      bkpt_node_t *n;
      while (data->bkpt_anchor->next) {
        n = data->bkpt_anchor->next;
        data->bkpt_anchor->next = n->next;
        csound->Free(csound, n); /* TODO this should be moved from kperf to a
                                    non-realtime context */
      }
      csound->Free(csound, bkpt_node);
    } else if (bkpt_node->mode == CSDEBUG_BKPT_DELETE) {
      bkpt_node_t *n = data->bkpt_anchor->next;
      bkpt_node_t *prev = data->bkpt_anchor;
      while (n) {
        if (n->line == bkpt_node->line && n->instr == bkpt_node->instr) {
          prev->next = n->next;
          if (data->cur_bkpt == n)
            data->cur_bkpt = n->next;
          csound->Free(csound, n); /* TODO this should be moved from kperf to a
                                      non-realtime context */
          n = prev->next;
          continue;
        }
        prev = n;
        n = n->next;
      }
      //        csound->Free(csound, bkpt_node); /* TODO move to non rt context
      //        */
    } else {
      // FIXME sort list to optimize
      bkpt_node->next = data->bkpt_anchor->next;
      data->bkpt_anchor->next = bkpt_node;
    }
  }
}

int32_t kperf_debug(CSOUND *csound) {
  INSDS *ip;
  csdebug_data_t *data = (csdebug_data_t *)csound->csdebug_data;
  int32_t lksmps = csound->ksmps;
  /* call message_dequeue to run API calls */
  message_dequeue(csound);

  if (!data || data->status != CSDEBUG_STATUS_STOPPED) {
    /* update orchestra time */
    csound->kcounter = ++(csound->global_kcounter);
    csound->icurTime += csound->ksmps;
    csound->curBeat += csound->curBeat_inc;
  }

  /* if skipping time on request by 'a' score statement: */
  if (UNLIKELY(csound->advanceCnt)) {
    csound->advanceCnt--;
    return 1;
  }
  /* if i-time only, return now */
  if (UNLIKELY(csound->initonly))
    return 1;
  /* PC GUI needs attention, but avoid excessively frequent */
  /* calls of csoundYield() */
  if (UNLIKELY(--(csound->evt_poll_cnt) < 0)) {
    csound->evt_poll_cnt = csound->evt_poll_maxcnt;
    if (UNLIKELY(!csoundYield(csound)))
      csound->LongJmp(csound, 1);
  }

  if (data) { /* process debug commands*/
    process_debug_buffers(csound, data);
  }

  if (!data || data->status == CSDEBUG_STATUS_RUNNING) {
    /* for one kcnt: */
    if (csound->oparms_.sfread) /*   if audio_infile open  */
      csound->spinrecv(csound); /*      fill the spin buf  */
    /* clear spout */
    memset(csound->spout, 0, csound->nspout * sizeof(MYFLT));
    memset(csound->spout_tmp, 0, csound->nspout * sizeof(MYFLT));
  }

  ip = csound->actanchor.nxtact;
  /* Process debugger commands */
  debug_command_t command = CSDEBUG_CMD_NONE;
  if (data) {
    csoundReadCircularBuffer(csound, data->cmd_buffer, &command, 1);
    if (command == CSDEBUG_CMD_STOP && data->status != CSDEBUG_STATUS_STOPPED) {
      data->debug_instr_ptr = ip;
      data->status = CSDEBUG_STATUS_STOPPED;
      csoundDebuggerBreakpointReached(csound);
    }
    if (command == CSDEBUG_CMD_CONTINUE &&
        data->status == CSDEBUG_STATUS_STOPPED) {
      if (data->cur_bkpt && data->cur_bkpt->skip <= 2)
        data->cur_bkpt->count = 2;
      data->status = CSDEBUG_STATUS_RUNNING;
      if (data->debug_instr_ptr) {
        /* if not NULL, resume from last active */
        ip = data->debug_instr_ptr;
        data->debug_instr_ptr = NULL;
      }
    }
    if (command == CSDEBUG_CMD_NEXT && data->status == CSDEBUG_STATUS_STOPPED) {
      data->status = CSDEBUG_STATUS_NEXT;
    }
  }
  if (ip != NULL && data != NULL && (data->status != CSDEBUG_STATUS_STOPPED)) {
    /* There are 2 partitions of work: 1st by inso,
       2nd by inso count / thread count. */
    if (csound->multiThreadedThreadInfo != NULL) {
#ifdef PARCS
      if (csound->dag_changed)
        dag_build(csound, ip);
      else
        dag_reinit(csound); /* set to initial state */

      /* process this partition */
      csound->WaitBarrier(csound->barrier1);

      (void)nodePerf(csound, 0, 1);

      /* wait until partition is complete */
      csound->WaitBarrier(csound->barrier2);
      // do the mixing of thread buffers
      {
        int32_t k;
        for (k = 1; k < csound->oparms->numThreads; k++)
          mix_out(csound->spout_tmp, csound->spout_tmp + k * csound->nspout,
                  csound->nspout);
      }
#endif
      csound->multiThreadedDag = NULL;
    } else {
      int32_t done;
      double time_end = (csound->ksmps + csound->icurTime) / csound->esr;

      while (ip != NULL) { /* for each instr active:  */
        if (UNLIKELY(csound->oparms->sampleAccurate && ip->offtim > 0 &&
                     time_end > ip->offtim)) {
          /* this is the last cycle of performance */
          //   csound->Message(csound, "last cycle %d: %f %f %d\n",
          //       ip->insno, csound->icurTime/csound->esr,
          //          ip->offtim, ip->no_end);
          ip->ksmps_no_end = ip->no_end;
        }
        done = ATOMIC_GET(ip->init_done);
        if (done == 1) { /* if init-pass has been done */
          /* check if next command pending and we are on the
             first instrument in the chain */
          /* coverity says data already dereferenced by here */
          if (/*data &&*/ data->status == CSDEBUG_STATUS_NEXT) {
            if (data->debug_instr_ptr == NULL) {
              data->debug_instr_ptr = ip;
              data->debug_opcode_ptr = NULL;
              data->status = CSDEBUG_STATUS_STOPPED;
              csoundDebuggerBreakpointReached(csound);
              return 0;
            } else {
              ip = data->debug_instr_ptr;
              data->debug_instr_ptr = NULL;
            }
          }
          /* check if we have arrived at an instrument breakpoint */
          bkpt_node_t *bp_node = data->bkpt_anchor->next;
          while (bp_node && data->status != CSDEBUG_STATUS_NEXT) {
            if (bp_node->instr == ip->p1.value && (bp_node->line == -1)) {
              if (bp_node->count < 2) {
                /* skip of 0 or 1 has the same effect */
                data->debug_instr_ptr = ip;
                data->debug_opcode_ptr = NULL;
                data->cur_bkpt = bp_node;
                data->status = CSDEBUG_STATUS_STOPPED;
                csoundDebuggerBreakpointReached(csound);
                bp_node->count = bp_node->skip;
                return 0;
              } else {
                bp_node->count--;
              }
            }
            bp_node = bp_node->next;
          }
          ip->spin = csound->spin;
          ip->spout = csound->spout_tmp;
          ip->kcounter = csound->kcounter;
          if (ip->ksmps == csound->ksmps) {
            opcode_perf_debug(csound, data, ip);
          } else { /* when instrument has local ksmps */
            int32_t i, n = csound->nspout, start = 0;
            lksmps = ip->ksmps;
            int32_t incr = csound->nchnls * lksmps;
            int32_t offset = ip->ksmps_offset;
            int32_t early = ip->ksmps_no_end;
            ip->spin = csound->spin;
            ip->kcounter = csound->kcounter * csound->ksmps / lksmps;

            /* we have to deal with sample-accurate code
               whole CS_KSMPS blocks are offset here, the
               remainder is left to each opcode to deal with.
            */
            while (offset >= lksmps) {
              offset -= lksmps;
              start += csound->nchnls;
            }
            ip->ksmps_offset = offset;
            if (UNLIKELY(early)) {
              n -= (early * csound->nchnls);
              ip->ksmps_no_end = early % lksmps;
            }

            for (i = start; i < n;
                 i += incr, ip->spin += incr, ip->spout += incr) {
              opcode_perf_debug(csound, data, ip);
              ip->kcounter++;
            }
          }
        }
        ip->ksmps_offset = 0; /* reset sample-accuracy offset */
        ip->ksmps_no_end = 0; /* reset end of loop samples */
        ip = ip->nxtact;      /* but this does not allow for all deletions */
        if (/*data &&*/ data->status == CSDEBUG_STATUS_NEXT) {
          data->debug_instr_ptr = ip; /* we have reached the next
                                         instrument. Break */
          data->debug_opcode_ptr = NULL;
          if (ip != NULL) { /* must defer break until next kperf */
            data->status = CSDEBUG_STATUS_STOPPED;
            csoundDebuggerBreakpointReached(csound);
            return 0;
          }
        }
      }
    }
  }

  if (!data || data->status != CSDEBUG_STATUS_STOPPED)
    csound->spoutran(csound); /*      send to audio_out  */

  return 0;
}

int32_t csoundReadScoreInternal(CSOUND *csound, const char *str) {
  /* protect resource */
  if (csound->scorestr != NULL && csound->scorestr->body != NULL)
    corfile_rewind(csound->scorestr);
  csound->scorestr = corfile_create_w(csound);
  corfile_puts(csound, (char *)str, csound->scorestr);
  // #ifdef SCORE_PARSER
  if (csound->engineStatus & CS_STATE_COMP)
    corfile_puts(csound, "\n#exit\n", csound->scorestr);
  else
    corfile_puts(csound, "\ne\n#exit\n", csound->scorestr);
  // #endif
  corfile_flush(csound, csound->scorestr);
  /* copy sorted score name */
  if (csound->scstr == NULL && (csound->engineStatus & CS_STATE_COMP) == 0) {
    scsortstr(csound, csound->scorestr);
    csound->playscore = csound->scstr;
    // corfile_rm(csound, &(csound->scorestr));
    // printf("%s\n", O->playscore->body);
  } else {

    char *sc = scsortstr(csound, csound->scorestr);
    csoundInputMessageInternal(csound, (const char *)sc);
    csound->Free(csound, sc);
    corfile_rm(csound, &(csound->scorestr));
  }
  return CSOUND_SUCCESS;
}

PUBLIC int32_t csoundPerformKsmps(CSOUND *csound) {
  int32_t done;
  /* VL: 1.1.13 if not compiled (csoundStart() not called)  */
  if (UNLIKELY(!(csound->engineStatus & CS_STATE_COMP))) {
    csound->Warning(csound,
                    Str("Csound not ready for performance: csoundStart() "
                        "has not been called\n"));
    return CSOUND_ERROR;
  }
  if (csound->jumpset == 0) {
    int32_t returnValue;
    csound->jumpset = 1;
    /* setup jmp for return after an exit() */
    if (UNLIKELY((returnValue = setjmp(csound->exitjmp))))
      return ((returnValue - CSOUND_EXITJMP_SUCCESS) | CSOUND_EXITJMP_SUCCESS);
  }
  if (!csound->oparms->realtime) // no API lock in realtime mode
    csoundLockMutex(csound->API_lock);
  do {
    done = sensevents(csound);
    if (UNLIKELY(done)) {
      if (!csound->oparms->realtime) // no API lock in realtime mode
        csoundUnlockMutex(csound->API_lock);
      csoundMessage(csound, Str("End of Performance "));
      return done;
    }
  } while (csound->kperf(csound));
  if (!csound->oparms->realtime) // no API lock in realtime mode
    csoundUnlockMutex(csound->API_lock);
  return 0;
}

/* external host's outbuffer passed in csoundPerformBuffer() */
PUBLIC int32_t csoundPerformBuffer(CSOUND *csound) {
  int32_t returnValue;
  int32_t done;
  /* VL: 1.1.13 if not compiled (csoundStart() not called)  */
  if (UNLIKELY(!(csound->engineStatus & CS_STATE_COMP))) {
    csound->Warning(csound,
                    Str("Csound not ready for performance: csoundStart() "
                        "has not been called\n"));
    return CSOUND_ERROR;
  }
  /* Setup jmp for return after an exit(). */
  if (UNLIKELY((returnValue = setjmp(csound->exitjmp)))) {
#ifndef MACOSX
    csoundMessage(csound, Str("Early return from csoundPerformBuffer().\n"));
#endif
    return ((returnValue - CSOUND_EXITJMP_SUCCESS) | CSOUND_EXITJMP_SUCCESS);
  }
  csound->sampsNeeded += csound->oparms_.outbufsamps;
  while (csound->sampsNeeded > 0) {
    if (!csound->oparms->realtime) { // no API lock in realtime mode
      csoundLockMutex(csound->API_lock);
    }
    do {
      if (UNLIKELY((done = sensevents(csound)))) {
        if (!csound->oparms->realtime) // no API lock in realtime mode
          csoundUnlockMutex(csound->API_lock);
        return done;
      }
    } while (csound->kperf(csound));
    if (!csound->oparms->realtime) { // no API lock in realtime mode
      csoundUnlockMutex(csound->API_lock);
    }
    csound->sampsNeeded -= csound->nspout;
  }
  return 0;
}

/* perform an entire score */

PUBLIC int32_t csoundPerform(CSOUND *csound) {
  int32_t done;
  int32_t returnValue;

  /* VL: 1.1.13 if not compiled (csoundStart() not called)  */
  if (UNLIKELY(!(csound->engineStatus & CS_STATE_COMP))) {
    csound->Warning(csound,
                    Str("Csound not ready for performance: csoundStart() "
                        "has not been called\n"));
    return CSOUND_ERROR;
  }

  csound->performState = 0;
  /* setup jmp for return after an exit() */
  if (UNLIKELY((returnValue = setjmp(csound->exitjmp)))) {
#ifndef MACOSX
    csoundMessage(csound, Str("Early return from csoundPerform().\n"));
#endif
    return ((returnValue - CSOUND_EXITJMP_SUCCESS) | CSOUND_EXITJMP_SUCCESS);
  }
  do {
    if (!csound->oparms->realtime)
      csoundLockMutex(csound->API_lock);
    do {
      if (UNLIKELY((done = sensevents(csound)))) {
        csoundMessage(csound, Str("Score finished in csoundPerform().\n"));
        if (!csound->oparms->realtime)
          csoundUnlockMutex(csound->API_lock);
        if (csound->oparms->numThreads > 1) {
          csound->multiThreadedComplete = 1;
          csound->WaitBarrier(csound->barrier1);
        }
        return done;
      }
    } while (csound->kperf(csound));
    if (!csound->oparms->realtime)
      csoundUnlockMutex(csound->API_lock);
  } while ((unsigned char)csound->performState == (unsigned char)'\0');
  csoundMessage(csound, Str("csoundPerform(): stopped.\n"));
  csound->performState = 0;
  return 0;
}

void *csoundGetNamedGens(CSOUND *csound) {
  return csound->namedgen;
}

void csoundStop(CSOUND *csound) {
  csound->performState = -1;
}

/*
 * New API functions
 */
PUBLIC int32_t csoundCompileTree(CSOUND *csound, TREE *root, int32_t async) {
  return csoundCompileTreeInternal(csound, root, async);
}

PUBLIC int32_t csoundCompileOrc(CSOUND *csound, const char *str,
                                int32_t async) {
  return csoundCompileOrcInternal(csound, str, async);
}

PUBLIC void csoundEventString(CSOUND *csound, const char *message,
                              int32_t async) {
  if (async) {
    csoundReadScoreAsync(csound, message);
  } else
    csoundReadScoreInternal(csound, message);
}

PUBLIC void csoundEvent(CSOUND *csound, int32_t type, MYFLT *params,
                        int32_t nparams, int32_t async) {
  char c = 'i';
  if (type == CS_TABLE_EVENT)
    c = 'f';
  else if (type == CS_END_EVENT)
    c = 'e';

  if (async)
    csoundScoreEventAsync(csound, c, params, nparams);
  else
    csoundScoreEventInternal(csound, c, params, nparams);
}

PUBLIC int32_t csoundCompileCSD(CSOUND *csound, const char *csd, int32_t mode) {
  if (mode)
    return csoundCompileCsdText(csound, csd);
  else
    return csoundCompileCsd(csound, csd);
}

PUBLIC void csoundSetHostAudioIO(CSOUND *csound) {
  csound->enableHostImplementedAudioIO = 1;
}

PUBLIC void csoundSetHostMIDIIO(CSOUND *csound) {
  csound->enableHostImplementedMIDIIO = 1;
}

PUBLIC uint32_t csoundGetChannels(CSOUND *csound, int32_t isInput) {
  if (isInput)
    return csoundGetNchnlsInput(csound);
  else
    return csoundGetNchnls(csound);
}

/*
 * ATTRIBUTES
 */

PUBLIC int64_t csoundGetCurrentTimeSamples(CSOUND *csound) {
  return csound->icurTime;
}

PUBLIC MYFLT csoundGetSr(CSOUND *csound) {
  return csound->esr;
}

PUBLIC MYFLT csoundGetKr(CSOUND *csound) {
  return csound->ekr;
}

PUBLIC uint32_t csoundGetKsmps(CSOUND *csound) {
  return csound->ksmps;
}

uint32_t csoundGetNchnls(CSOUND *csound) {
  return csound->nchnls;
}

uint32_t csoundGetNchnlsInput(CSOUND *csound) {
  if (csound->inchnls >= 0)
    return (uint32_t)csound->inchnls;
  else
    return csound->nchnls;
}

PUBLIC MYFLT csoundGet0dBFS(CSOUND *csound) {
  return csound->e0dbfs;
}

long csoundGetInputBufferSize(CSOUND *csound) {
  return csound->oparms_.inbufsamps;
}

long csoundGetOutputBufferSize(CSOUND *csound) {
  return csound->oparms_.outbufsamps;
}

PUBLIC MYFLT *csoundGetSpin(CSOUND *csound) {
  return csound->spin;
}

void csoundSetSpinSample(CSOUND *csound, int32_t frame, int32_t channel,
                         MYFLT sample) {
  int32_t index = (frame * csound->inchnls) + channel;
  csound->spin[index] = sample;
}

void csoundClearSpin(CSOUND *csound) {

  memset(csound->spin, 0, sizeof(MYFLT) * csound->ksmps * csound->nchnls);
}

void csoundAddSpinSample(CSOUND *csound, int32_t frame, int32_t channel,
                         MYFLT sample) {

  int32_t index = (frame * csound->inchnls) + channel;
  csound->spin[index] += sample;
}

PUBLIC const MYFLT *csoundGetSpout(CSOUND *csound) {
  return csound->spout;
}

MYFLT csoundGetSpoutSample(CSOUND *csound, int32_t frame, int32_t channel) {
  int32_t index = (frame * csound->nchnls) + channel;
  return csound->spout[index];
}

PUBLIC const char *csoundGetOutputName(CSOUND *csound) {
  return (const char *)csound->oparms_.outfilename;
}

PUBLIC const char *csoundGetInputName(CSOUND *csound) {
  return (const char *)csound->oparms_.infilename;
}

/**
 * Calling this function with a non-zero will disable all default
 * handling of sound I/O by the Csound library, allowing the host
 * application to use the spin/<spout/input/output buffers directly.
 * If 'bufSize' is greater than zero, the buffer size (-b) will be
 * set to the integer multiple of ksmps that is nearest to the value
 * specified.
 */

PUBLIC void csoundSetHostImplementedAudioIO(CSOUND *csound, int32_t state,
                                            int32_t bufSize) {
  csound->enableHostImplementedAudioIO = state;
  csound->hostRequestedBufferSize = (bufSize > 0 ? bufSize : 0);
}

PUBLIC void csoundSetHostImplementedMIDIIO(CSOUND *csound, int32_t state) {
  csound->enableHostImplementedMIDIIO = state;
}

PUBLIC double csoundGetScoreTime(CSOUND *csound) {
  double curtime = csound->icurTime;
  double esr = csound->esr;
  return curtime / esr;
}

/*
 * SCORE HANDLING
 */

PUBLIC int32_t csoundIsScorePending(CSOUND *csound) {
  return csound->csoundIsScorePending_;
}

PUBLIC void csoundSetScorePending(CSOUND *csound, int32_t pending) {
  csound->csoundIsScorePending_ = pending;
}

PUBLIC void csoundSetScoreOffsetSeconds(CSOUND *csound, MYFLT offset) {
  double aTime;
  MYFLT prv = (MYFLT)csound->csoundScoreOffsetSeconds_;

  csound->csoundScoreOffsetSeconds_ = offset;
  if (offset < FL(0.0))
    return;
  /* if csoundCompile() was not called yet, just store the offset */
  if (!(csound->engineStatus & CS_STATE_COMP))
    return;
  /* otherwise seek to the requested time now */
  aTime = (double)offset - (csound->icurTime / csound->esr);
  if (aTime < 0.0 || offset < prv) {
    csoundRewindScore(csound); /* will call csoundSetScoreOffsetSeconds */
    return;
  }
  if (aTime > 0.0) {
    EVTBLK evt;
    memset(&evt, 0, sizeof(EVTBLK));
    evt.strarg = NULL;
    evt.scnt = 0;
    evt.opcod = 'a';
    evt.pcnt = 3;
    evt.p[2] = evt.p[1] = FL(0.0);
    evt.p[3] = (MYFLT)aTime;
    insert_score_event_at_sample(csound, &evt, csound->icurTime);
  }
}

PUBLIC MYFLT csoundGetScoreOffsetSeconds(CSOUND *csound) {
  return csound->csoundScoreOffsetSeconds_;
}

extern void musmon_rewind_score(CSOUND *csound);   /* musmon.c */
extern void midifile_rewind_score(CSOUND *csound); /* midifile.c */

PUBLIC void csoundRewindScore(CSOUND *csound) {
  musmon_rewind_score(csound);
  if (csound->oparms->FMidiname != NULL)
    midifile_rewind_score(csound);
}

PUBLIC void csoundSetCscoreCallback(CSOUND *p,
                                    void (*cscoreCallback)(CSOUND *)) {
  p->cscoreCallback_ = (cscoreCallback != NULL ? cscoreCallback : cscore_);
}

static void csoundDefaultMessageCallback(CSOUND *csound, int32_t attr,
                                         const char *format, va_list args) {
#if defined(WIN32)
  switch (attr & CSOUNDMSG_TYPE_MASK) {
  case CSOUNDMSG_ERROR:
  case CSOUNDMSG_WARNING:
  case CSOUNDMSG_REALTIME:
    vfprintf(stderr, format, args);
    break;
  default:
    vfprintf(stdout, format, args);
  }
#else
  FILE *fp = stderr;
  if ((attr & CSOUNDMSG_TYPE_MASK) == CSOUNDMSG_STDOUT)
    fp = stdout;
  if (!attr || !csound->enableMsgAttr) {
    vfprintf(fp, format, args);
    return;
  }
  if ((attr & CSOUNDMSG_TYPE_MASK) == CSOUNDMSG_ORCH)
    if (attr & CSOUNDMSG_BG_COLOR_MASK)
      fprintf(fp, "\033[4%cm", ((attr & 0x70) >> 4) + '0');
  if (attr & CSOUNDMSG_FG_ATTR_MASK) {
    if (attr & CSOUNDMSG_FG_BOLD)
      fprintf(fp, "\033[1m");
    if (attr & CSOUNDMSG_FG_UNDERLINE)
      fprintf(fp, "\033[4m");
  }
  if (attr & CSOUNDMSG_FG_COLOR_MASK)
    fprintf(fp, "\033[3%cm", (attr & 7) + '0');
  vfprintf(fp, format, args);
  fprintf(fp, "\033[m");
#endif
}

PUBLIC void csoundSetDefaultMessageCallback(void (*csoundMessageCallback)(
    CSOUND *csound, int32_t attr, const char *format, va_list args)) {
  if (csoundMessageCallback) {
    msgcallback_ = csoundMessageCallback;
  } else {
    msgcallback_ = csoundDefaultMessageCallback;
  }
}

PUBLIC void csoundSetMessageStringCallback(
    CSOUND *csound,
    void (*csoundMessageStrCallback)(CSOUND *csound, int32_t attr,
                                     const char *str)) {

  if (csoundMessageStrCallback) {
    if (csound->message_string == NULL)
      csound->message_string = (char *)csound->Calloc(csound, MAX_MESSAGE_STR);
    csound->csoundMessageStringCallback = csoundMessageStrCallback;
    csound->csoundMessageCallback_ = NULL;
  }
}

PUBLIC void csoundSetMessageCallback(
    CSOUND *csound,
    void (*csoundMessageCallback)(CSOUND *csound, int32_t attr,
                                  const char *format, va_list args)) {
  /* Protect against a null callback. */
  if (csoundMessageCallback) {
    csound->csoundMessageCallback_ = csoundMessageCallback;
  } else {
    csound->csoundMessageCallback_ = csoundDefaultMessageCallback;
  }
}

PUBLIC void csoundMessageV(CSOUND *csound, int32_t attr, const char *format,
                           va_list args) {
  if (!(csound->oparms->msglevel & CS_NOMSG)) {
    if (csound->csoundMessageCallback_) {
      csound->csoundMessageCallback_(csound, attr, format, args);
    } else {
      vsnprintf(csound->message_string, MAX_MESSAGE_STR, format, args);
      csound->csoundMessageStringCallback(csound, attr, csound->message_string);
    }
  }
}

PUBLIC void csoundMessage(CSOUND *csound, const char *format, ...) {
  if (!(csound->oparms->msglevel & CS_NOMSG)) {
    va_list args;
    va_start(args, format);
    if (csound->csoundMessageCallback_)
      csound->csoundMessageCallback_(csound, 0, format, args);
    else {
      vsnprintf(csound->message_string, MAX_MESSAGE_STR, format, args);
      csound->csoundMessageStringCallback(csound, 0, csound->message_string);
    }
    va_end(args);
  }
}

PUBLIC void csoundMessageS(CSOUND *csound, int32_t attr, const char *format,
                           ...) {
  if (!(csound->oparms->msglevel & CS_NOMSG)) {
    va_list args;
    va_start(args, format);
    if (csound->csoundMessageCallback_)
      csound->csoundMessageCallback_(csound, attr, format, args);
    else {
      vsnprintf(csound->message_string, MAX_MESSAGE_STR, format, args);
      csound->csoundMessageStringCallback(csound, attr, csound->message_string);
    }
    va_end(args);
  }
}

void csoundDie(CSOUND *csound, const char *msg, ...) {
  va_list args;
  va_start(args, msg);
  csound->ErrMsgV(csound, (char *)0, msg, args);
  va_end(args);
  csound->perferrcnt++;
  csound->LongJmp(csound, 1);
}

void csoundWarning(CSOUND *csound, const char *msg, ...) {
  va_list args;
  if (!(csound->oparms_.msglevel & CS_WARNMSG))
    return;
  csoundMessageS(csound, CSOUNDMSG_WARNING, Str("WARNING: "));
  va_start(args, msg);
  csoundMessageV(csound, CSOUNDMSG_WARNING, msg, args);
  va_end(args);
  csoundMessageS(csound, CSOUNDMSG_WARNING, "\n");
}

void csoundDebugMsg(CSOUND *csound, const char *msg, ...) {
  va_list args;
  if (!(csound->oparms_.odebug & (~CS_NOQQ)))
    return;
  va_start(args, msg);
  csoundMessageV(csound, 0, msg, args);
  va_end(args);
  csoundMessage(csound, "\n");
}

void csoundErrorMsg(CSOUND *csound, const char *msg, ...) {
  va_list args;
  va_start(args, msg);
  csoundMessageV(csound, CSOUNDMSG_ERROR, msg, args);
  va_end(args);
}

void csoundErrMsgV(CSOUND *csound, const char *hdr, const char *msg,
                   va_list args) {
  if (hdr != NULL)
    csound->MessageS(csound, CSOUNDMSG_ERROR, "%s", hdr);
  csoundMessageV(csound, CSOUNDMSG_ERROR, msg, args);
  csound->MessageS(csound, CSOUNDMSG_ERROR, "\n");
}

void csoundErrorMsgS(CSOUND *csound, int32_t attr, const char *msg, ...) {
  va_list args;
  va_start(args, msg);
  csoundMessageV(csound, CSOUNDMSG_ERROR | attr, msg, args);
  va_end(args);
}

void csoundLongJmp(CSOUND *csound, int32_t retval) {
  int32_t n = CSOUND_EXITJMP_SUCCESS;

  n = (retval < 0 ? n + retval : n - retval) & (CSOUND_EXITJMP_SUCCESS - 1);
  // printf("**** n = %d\n", n);
  if (!n)
    n = CSOUND_EXITJMP_SUCCESS;

  csound->curip = NULL;
  csound->ids = NULL;
  csound->reinitflag = 0;
  csound->tieflag = 0;
  csound->perferrcnt += csound->inerrcnt;
  csound->inerrcnt = 0;
  csound->engineStatus |= CS_STATE_JMP;
  // printf("**** longjmp with %d\n", n);

  longjmp(csound->exitjmp, n);
}

PUBLIC void csoundSetMessageLevel(CSOUND *csound, int32_t messageLevel) {
  csound->oparms_.msglevel = messageLevel;
}

PUBLIC int32_t csoundGetMessageLevel(CSOUND *csound) {
  return csound->oparms_.msglevel;
}

PUBLIC void csoundKeyPress(CSOUND *csound, char c) {
  csound->inChar_ = (int32_t)((unsigned char)c);
}

/*
 * CONTROL AND EVENTS
 */

PUBLIC void
csoundSetInputChannelCallback(CSOUND *csound,
                              channelCallback_t inputChannelCalback) {
  csound->InputChannelCallback_ = inputChannelCalback;
}

PUBLIC void
csoundSetOutputChannelCallback(CSOUND *csound,
                               channelCallback_t outputChannelCalback) {
  csound->OutputChannelCallback_ = outputChannelCalback;
}

int32_t csoundScoreEventInternal(CSOUND *csound, char type,
                                 const MYFLT *pfields, long numFields) {
  EVTBLK evt;
  int32_t i;
  int32_t ret;
  memset(&evt, 0, sizeof(EVTBLK));

  evt.strarg = NULL;
  evt.scnt = 0;
  evt.opcod = type;
  evt.pcnt = (int16)numFields;
  for (i = 0; i < (int32_t)numFields; i++)
    evt.p[i + 1] = pfields[i];
  ret = insert_score_event_at_sample(csound, &evt, csound->icurTime);
  return ret;
}

int32_t csoundScoreEventAbsoluteInternal(CSOUND *csound, char type,
                                         const MYFLT *pfields, long numFields,
                                         double time_ofs) {
  EVTBLK evt;
  int32_t i;
  int32_t ret;
  memset(&evt, 0, sizeof(EVTBLK));

  evt.strarg = NULL;
  evt.scnt = 0;
  evt.opcod = type;
  evt.pcnt = (int16)numFields;
  for (i = 0; i < (int32_t)numFields; i++)
    evt.p[i + 1] = pfields[i];
  ret = insert_score_event(csound, &evt, time_ofs);
  return ret;
}

/*
 *    REAL-TIME AUDIO
 */

/* dummy functions for the case when no real-time audio module is available */

static double *get_dummy_rtaudio_globals(CSOUND *csound) {
  double *p;

  p = (double *)csound->QueryGlobalVariable(csound, "__rtaudio_null_state");
  if (p == NULL) {
    if (UNLIKELY(csound->CreateGlobalVariable(csound, "__rtaudio_null_state",
                                              sizeof(double) * 4) != 0))
      csound->Die(csound, Str("rtdummy: failed to allocate globals"));
    csound->Message(csound, Str("rtaudio: dummy module enabled\n"));
    p = (double *)csound->QueryGlobalVariable(csound, "__rtaudio_null_state");
  }
  return p;
}

static void dummy_rtaudio_timer(CSOUND *csound, double *p) {
  double timeWait;
  int32_t i;

  timeWait = p[0] - csoundGetRealTime(csound->csRtClock);
  i = (int32_t)(timeWait * 1000.0 + 0.5);
  if (i > 0)
    csoundSleep((size_t)i);
}

int32_t playopen_dummy(CSOUND *csound, const csRtAudioParams *parm) {
  double *p;
  char *s;

  /* find out if the use of dummy real-time audio functions was requested, */
  /* or an unknown plugin name was specified; the latter case is an error  */
  s = (char *)csoundQueryGlobalVariable(csound, "_RTAUDIO");
  if (s != NULL && !(strcmp(s, "null") == 0 || strcmp(s, "Null") == 0 ||
                     strcmp(s, "NULL") == 0)) {
    if (s[0] == '\0')
      csoundErrorMsg(csound,
                     Str(" *** error: rtaudio module set to empty string"));
    else {
      // print_opcodedir_warning(csound);
      csoundErrorMsg(
          csound, Str(" unknown rtaudio module: '%s', using dummy module"), s);
    }
    // return CSOUND_ERROR;
  }
  p = get_dummy_rtaudio_globals(csound);
  csound->rtPlay_userdata = (void *)p;
  p[0] = csound->GetRealTime(csound->csRtClock);
  p[1] = 1.0 / ((double)((int32_t)sizeof(MYFLT) * parm->nChannels) *
                (double)parm->sampleRate);
  return CSOUND_SUCCESS;
}

void rtplay_dummy(CSOUND *csound, const MYFLT *outBuf, int32_t nbytes) {
  double *p = (double *)csound->rtPlay_userdata;
  (void)outBuf;
  p[0] += ((double)nbytes * p[1]);
  dummy_rtaudio_timer(csound, p);
}

int32_t recopen_dummy(CSOUND *csound, const csRtAudioParams *parm) {
  double *p;
  char *s;

  /* find out if the use of dummy real-time audio functions was requested, */
  /* or an unknown plugin name was specified; the latter case is an error  */
  s = (char *)csoundQueryGlobalVariable(csound, "_RTAUDIO");
  if (s != NULL && !(strcmp(s, "null") == 0 || strcmp(s, "Null") == 0 ||
                     strcmp(s, "NULL") == 0)) {
    if (s[0] == '\0')
      csoundErrorMsg(csound,
                     Str(" *** error: rtaudio module set to empty string"));
    else {
      // print_opcodedir_warning(csound);
      csoundErrorMsg(
          csound, Str(" unknown rtaudio module: '%s', using dummy module"), s);
    }
    // return CSOUND_ERROR;
  }
  p = (double *)get_dummy_rtaudio_globals(csound) + 2;
  csound->rtRecord_userdata = (void *)p;
  p[0] = csound->GetRealTime(csound->csRtClock);
  p[1] = 1.0 / ((double)((int32_t)sizeof(MYFLT) * parm->nChannels) *
                (double)parm->sampleRate);
  return CSOUND_SUCCESS;
}

int32_t rtrecord_dummy(CSOUND *csound, MYFLT *inBuf, int32_t nbytes) {
  double *p = (double *)csound->rtRecord_userdata;

  /* for (i = 0; i < (nbytes / (int32_t) sizeof(MYFLT)); i++) */
  /*   ((MYFLT*) inBuf)[i] = FL(0.0); */
  memset(inBuf, 0, nbytes);

  p[0] += ((double)nbytes * p[1]);
  dummy_rtaudio_timer(csound, p);

  return nbytes;
}

void rtclose_dummy(CSOUND *csound) {
  csound->rtPlay_userdata = NULL;
  csound->rtRecord_userdata = NULL;
}

int32_t audio_dev_list_dummy(CSOUND *csound, CS_AUDIODEVICE *list,
                             int32_t isOutput) {
  IGN(csound);
  IGN(list);
  IGN(isOutput);
  return 0;
}

int32_t midi_dev_list_dummy(CSOUND *csound, CS_MIDIDEVICE *list,
                            int32_t isOutput) {
  IGN(csound);
  IGN(list);
  IGN(isOutput);
  return 0;
}

void csoundSetPlayopenCallback(
    CSOUND *csound,
    int32_t (*playopen__)(CSOUND *, const csRtAudioParams *parm)) {
  csound->playopen_callback = playopen__;
}

void csoundSetRtplayCallback(CSOUND *csound,
                             void (*rtplay__)(CSOUND *, const MYFLT *outBuf,
                                              int32_t nbytes)) {
  csound->rtplay_callback = rtplay__;
}

void csoundSetRecopenCallback(
    CSOUND *csound,
    int32_t (*recopen__)(CSOUND *, const csRtAudioParams *parm)) {
  csound->recopen_callback = recopen__;
}

void csoundSetRtrecordCallback(CSOUND *csound,
                               int32_t (*rtrecord__)(CSOUND *, MYFLT *inBuf,
                                                     int32_t nbytes)) {
  csound->rtrecord_callback = rtrecord__;
}

void csoundSetRtcloseCallback(CSOUND *csound, void (*rtclose__)(CSOUND *)) {
  csound->rtclose_callback = rtclose__;
}

void csoundSetAudioDeviceListCallback(
    CSOUND *csound, int32_t (*audiodevlist__)(CSOUND *, CS_AUDIODEVICE *list,
                                              int32_t isOutput)) {
  csound->audio_dev_list_callback = audiodevlist__;
}

PUBLIC void csoundSetMIDIDeviceListCallback(
    CSOUND *csound,
    int32_t (*mididevlist__)(CSOUND *, CS_MIDIDEVICE *list, int32_t isOutput)) {
  csound->midi_dev_list_callback = mididevlist__;
}

PUBLIC int32_t csoundGetAudioDevList(CSOUND *csound, CS_AUDIODEVICE *list,
                                     int32_t isOutput) {
  return csound->audio_dev_list_callback(csound, list, isOutput);
}

PUBLIC int32_t csoundGetMIDIDevList(CSOUND *csound, CS_MIDIDEVICE *list,
                                    int32_t isOutput) {
  return csound->midi_dev_list_callback(csound, list, isOutput);
}

/* dummy real time MIDI functions */
int32_t DummyMidiInOpen(CSOUND *csound, void **userData, const char *devName) {
  char *s;

  (void)devName;
  *userData = NULL;
  s = (char *)csoundQueryGlobalVariable(csound, "_RTMIDI");
  if (UNLIKELY(s == NULL || (strcmp(s, "null") == 0 || strcmp(s, "Null") == 0 ||
                             strcmp(s, "NULL") == 0))) {
    csoundMessage(csound, Str("!!WARNING: real time midi input disabled, "
                              "using dummy functions\n"));
    return 0;
  }
  if (s[0] == '\0')
    csoundErrorMsg(csound, Str("error: -+rtmidi set to empty string"));
  else {
    print_opcodedir_warning(csound);
    csoundErrorMsg(csound, Str("error: -+rtmidi='%s': unknown module"), s);
  }
  return -1;
}

int32_t DummyMidiRead(CSOUND *csound, void *userData, unsigned char *buf,
                      int32_t nbytes) {
  (void)csound;
  (void)userData;
  (void)buf;
  (void)nbytes;
  return 0;
}

int32_t DummyMidiOutOpen(CSOUND *csound, void **userData, const char *devName) {
  char *s;

  (void)devName;
  *userData = NULL;
  s = (char *)csoundQueryGlobalVariable(csound, "_RTMIDI");
  if (s == NULL || (strcmp(s, "null") == 0 || strcmp(s, "Null") == 0 ||
                    strcmp(s, "NULL") == 0)) {
    csoundMessage(csound, Str("WARNING: real time midi output disabled, "
                              "using dummy functions\n"));
    return 0;
  }
  if (s[0] == '\0')
    csoundErrorMsg(csound, Str("error: -+rtmidi set to empty string"));
  else {
    print_opcodedir_warning(csound);
    csoundErrorMsg(csound, Str("error: -+rtmidi='%s': unknown module"), s);
  }
  return -1;
}

int32_t DummyMidiWrite(CSOUND *csound, void *userData, const unsigned char *buf,
                       int32_t nbytes) {
  (void)csound;
  (void)userData;
  (void)buf;
  return nbytes;
}

static const char *midi_err_msg = Str_noop("Unknown MIDI error");

/**
 * Returns pointer to a string constant storing an error massage
 * for error code 'errcode'.
 */
const char *csoundExternalMidiErrorString(CSOUND *csound, int32_t errcode) {
  if (csound->midiGlobals->MidiErrorStringCallback == NULL)
    return midi_err_msg;
  return (csound->midiGlobals->MidiErrorStringCallback(errcode));
}

/* Set real time MIDI function pointers. */

PUBLIC void csoundSetExternalMidiInOpenCallback(
    CSOUND *csound, int32_t (*func)(CSOUND *, void **, const char *)) {
  csound->midiGlobals->MidiInOpenCallback = func;
}

PUBLIC void csoundSetExternalMidiReadCallback(CSOUND *csound,
                                              int32_t (*func)(CSOUND *, void *,
                                                              unsigned char *,
                                                              int32_t)) {
  csound->midiGlobals->MidiReadCallback = func;
}

PUBLIC void csoundSetExternalMidiInCloseCallback(CSOUND *csound,
                                                 int32_t (*func)(CSOUND *,
                                                                 void *)) {
  csound->midiGlobals->MidiInCloseCallback = func;
}

PUBLIC void csoundSetExternalMidiOutOpenCallback(
    CSOUND *csound, int32_t (*func)(CSOUND *, void **, const char *)) {
  csound->midiGlobals->MidiOutOpenCallback = func;
}

PUBLIC void csoundSetExternalMidiWriteCallback(
    CSOUND *csound,
    int32_t (*func)(CSOUND *, void *, const unsigned char *, int32_t)) {
  csound->midiGlobals->MidiWriteCallback = func;
}

PUBLIC void csoundSetExternalMidiOutCloseCallback(CSOUND *csound,
                                                  int32_t (*func)(CSOUND *,
                                                                  void *)) {
  csound->midiGlobals->MidiOutCloseCallback = func;
}

PUBLIC void
csoundSetExternalMidiErrorStringCallback(CSOUND *csound,
                                         const char *(*func)(int32_t)) {
  csound->midiGlobals->MidiErrorStringCallback = func;
}

/*
 *    FUNCTION TABLE DISPLAY.
 */

PUBLIC int32_t csoundSetIsGraphable(CSOUND *csound, int32_t isGraphable) {
  int32_t prv = csound->isGraphable_;
  csound->isGraphable_ = isGraphable;
  return prv;
}

PUBLIC void csoundSetMakeGraphCallback(CSOUND *csound,
                                       void (*makeGraphCB)(CSOUND *csound,
                                                           WINDAT *windat,
                                                           const char *name)) {
  csound->csoundMakeGraphCallback_ = makeGraphCB;
}

PUBLIC void csoundSetDrawGraphCallback(
    CSOUND *csound, void (*drawGraphCallback)(CSOUND *csound, WINDAT *windat)) {
  csound->csoundDrawGraphCallback_ = drawGraphCallback;
}

PUBLIC void csoundSetKillGraphCallback(
    CSOUND *csound, void (*killGraphCallback)(CSOUND *csound, WINDAT *windat)) {
  csound->csoundKillGraphCallback_ = killGraphCallback;
}

PUBLIC void csoundSetExitGraphCallback(CSOUND *csound,
                                       int32_t (*exitGraphCallback)(CSOUND *)) {
  csound->csoundExitGraphCallback_ = exitGraphCallback;
}

/*
 * OPCODES
 */
// void add_to_symbtab(CSOUND *csound, OENTRY *ep);

static CS_NOINLINE int32_t opcode_list_new_oentry(CSOUND *csound,
                                                  const OENTRY *ep) {
  CONS_CELL *head;
  OENTRY *entryCopy;
  char *shortName;

  if (UNLIKELY(ep->opname == NULL || csound->opcodes == NULL))
    return CSOUND_ERROR;

  shortName = get_opcode_short_name(csound, ep->opname);
  head = cs_hash_table_get(csound, csound->opcodes, shortName);
  entryCopy = csound->Malloc(csound, sizeof(OENTRY));
  memcpy(entryCopy, ep, sizeof(OENTRY));
  entryCopy->useropinfo = NULL;

  if (head != NULL) {
    cs_cons_append(head, cs_cons(csound, entryCopy, NULL));
  } else {
    head = cs_cons(csound, entryCopy, NULL);
    cs_hash_table_put(csound, csound->opcodes, shortName, head);
  }

  if (shortName != ep->opname) {
    csound->Free(csound, shortName);
  }

  return 0;
}

PUBLIC int32_t csoundAppendOpcode(CSOUND *csound, const char *opname,
                                  int32_t dsblksiz, int32_t flags,
                                  const char *outypes, const char *intypes,
                                  int32_t (*init)(CSOUND *, void *),
                                  int32_t (*perf)(CSOUND *, void *),
                                  int32_t (*deinit)(CSOUND *, void *)) {
  OENTRY tmpEntry;
  int32_t err;
  tmpEntry.opname = (char *)opname;
  tmpEntry.dsblksiz = (uint16)dsblksiz;
  tmpEntry.flags = (uint16)flags;
  tmpEntry.outypes = (char *)outypes;
  tmpEntry.intypes = (char *)intypes;
  tmpEntry.init = init;
  tmpEntry.perf = perf;
  tmpEntry.deinit = deinit;
  err = opcode_list_new_oentry(csound, &tmpEntry);
  // add_to_symbtab(csound, &tmpEntry);
  if (UNLIKELY(err))
    csoundErrorMsg(csound, Str("Failed to allocate new opcode entry."));
  return err;
}

/**
 * Appends a list of opcodes implemented by external software to Csound's
 * internal opcode list. The list should either be terminated with an entry
 * that has a NULL opname, or the number of entries (> 0) should be specified
 * in 'n'. Returns zero on success.
 */

int32_t csoundAppendOpcodes(CSOUND *csound, const OENTRY *opcodeList,
                            int32_t n) {
  OENTRY *ep = (OENTRY *)opcodeList;
  int32_t err, retval = 0;

  if (UNLIKELY(opcodeList == NULL))
    return -1;
  if (UNLIKELY(n <= 0))
    n = 0x7FFFFFFF;
  while (n && ep->opname != NULL) {
    if (UNLIKELY((err = opcode_list_new_oentry(csound, ep)) != 0)) {
      csoundErrorMsg(csound, Str("Failed to allocate opcode entry for %s."),
                     ep->opname);
      retval = err;
    }

    n--, ep++;
  }
  return retval;
}

/*
 * MISC FUNCTIONS
 */

int32_t defaultCsoundYield(CSOUND *csound) {
  (void)csound;
  return 1;
}

void csoundSetYieldCallback(CSOUND *csound,
                            int32_t (*yieldCallback)(CSOUND *)) {
  csound->csoundYieldCallback_ = yieldCallback;
}

int32_t csoundYield(CSOUND *csound) {
  if (exitNow_)
    csound->LongJmp(csound, CSOUND_SIGNAL);
  csound->csoundInternalYieldCallback_(csound);
  return csound->csoundYieldCallback_(csound);
}

extern void csoundDeleteAllGlobalVariables(CSOUND *csound);

typedef struct resetCallback_s {
  void *userData;
  int32_t (*func)(CSOUND *, void *);
  struct resetCallback_s *nxt;
} resetCallback_t;

static void reset(CSOUND *csound) {
  CSOUND *saved_env;
  void *p1, *p2;
  uintptr_t length;
  uintptr_t end, start;
  int32_t n = 0;

  csoundCleanup(csound);

  /* call registered reset callbacks */
  while (csound->reset_list != NULL) {
    resetCallback_t *p = (resetCallback_t *)csound->reset_list;
    p->func(csound, p->userData);
    csound->reset_list = (void *)p->nxt;
    free(p);
  }
  /* call local destructor routines of external modules */
  /* should check return value... */
  csoundDestroyModules(csound);

  /* IV - Feb 01 2005: clean up configuration variables and */
  /* named dynamic "global" variables of Csound instance */
  csoundDeleteAllConfigurationVariables(csound);
  csoundDeleteAllGlobalVariables(csound);

#ifdef CSCORE
  cscoreRESET(csound);
#endif
  if (csound->opcodes != NULL) {
    free_opcode_table(csound);
    csound->opcodes = NULL;
  }

  csound->oparms_.odebug = 0;
  /* RWD 9:2000 not terribly vital, but good to do this somewhere... */
  pvsys_release(csound);
  close_all_files(csound);
  /* delete temporary files created by this Csound instance */
  remove_tmpfiles(csound);
  rlsmemfiles(csound);

  while (csound->filedir[n]) /* Clear source directory */
    csound->Free(csound, csound->filedir[n++]);

  memRESET(csound);

  /**
   * Copy everything EXCEPT the function pointers.
   * We do it by saving them and copying them back again...
   * hope that this does not fail...
   */
  /* VL 07.06.2013 - check if the status is COMP before
     resetting.
  */
  // CSOUND **self = csound->self;
  saved_env = (CSOUND *)malloc(sizeof(CSOUND));
  memcpy(saved_env, csound, sizeof(CSOUND));
  memcpy(csound, &cenviron_, sizeof(CSOUND));
  end = (uintptr_t) & (csound->first_callback_); /* used to be &(csound->ids) */
  start = (uintptr_t)csound;
  length = end - start;
  memcpy((void *)csound, (void *)saved_env, (size_t)length);
  csound->oparms = &(csound->oparms_);
  csound->hostdata = saved_env->hostdata;
  csound->opcodedir = saved_env->opcodedir;
  p1 = (void *)&(csound->first_callback_);
  p2 = (void *)&(csound->last_callback_);
  length = (uintptr_t)p2 - (uintptr_t)p1;
  memcpy(p1, (void *)&(saved_env->first_callback_), (size_t)length);
  csound->csoundCallbacks_ = saved_env->csoundCallbacks_;
  csound->API_lock = saved_env->API_lock;
#ifdef HAVE_PTHREAD_SPIN_LOCK
  csound->memlock = saved_env->memlock;
  csound->spinlock = saved_env->spinlock;
  csound->spoutlock = saved_env->spoutlock;
  csound->spinlock1 = saved_env->spinlock1;
#endif
  csound->enableHostImplementedMIDIIO = saved_env->enableHostImplementedMIDIIO;
  memcpy(&(csound->exitjmp), &(saved_env->exitjmp), sizeof(jmp_buf));
  csound->memalloc_db = saved_env->memalloc_db;
  csound->message_buffer =
      saved_env->message_buffer; /*VL 19.06.21 keep msg buffer */
  // csound->self = self;
  free(saved_env);
}

PUBLIC void csoundSetRTAudioModule(CSOUND *csound, const char *module) {
  char *s;
  if ((s = csoundQueryGlobalVariable(csound, "_RTAUDIO")) != NULL)
    strNcpy(s, module, 20);
  if (UNLIKELY(s == NULL))
    return; /* Should not happen */
  if (strcmp(s, "null") == 0 || strcmp(s, "Null") == 0 ||
      strcmp(s, "NULL") == 0) {
    csound->Message(csound, Str("setting dummy interface\n"));
    csound->SetPlayopenCallback(csound, playopen_dummy);
    csound->SetRecopenCallback(csound, recopen_dummy);
    csound->SetRtplayCallback(csound, rtplay_dummy);
    csound->SetRtrecordCallback(csound, rtrecord_dummy);
    csound->SetRtcloseCallback(csound, rtclose_dummy);
    csound->SetAudioDeviceListCallback(csound, audio_dev_list_dummy);
    return;
  }
  if (csoundInitModules(csound) != 0)
    csound->LongJmp(csound, 1);
}

PUBLIC void csoundSetMIDIModule(CSOUND *csound, const char *module) {
  char *s;

  if ((s = csoundQueryGlobalVariable(csound, "_RTMIDI")) != NULL)
    strNcpy(s, module, 20);
  if (UNLIKELY(s == NULL))
    return; /* Should not happen */
  if (strcmp(s, "null") == 0 || strcmp(s, "Null") == 0 ||
      strcmp(s, "NULL") == 0) {
    csound->SetMIDIDeviceListCallback(csound, midi_dev_list_dummy);
    csound->SetExternalMidiInOpenCallback(csound, DummyMidiInOpen);
    csound->SetExternalMidiReadCallback(csound, DummyMidiRead);
    csound->SetExternalMidiInCloseCallback(csound, NULL);
    csound->SetExternalMidiOutOpenCallback(csound, DummyMidiOutOpen);
    csound->SetExternalMidiWriteCallback(csound, DummyMidiWrite);
    csound->SetExternalMidiOutCloseCallback(csound, NULL);

    return;
  }
  if (csoundInitModules(csound) != 0)
    csound->LongJmp(csound, 1);
}

PUBLIC int32_t csoundGetModule(CSOUND *csound, int32_t no, char **module,
                               char **type) {
  MODULE_INFO **modules =
      (MODULE_INFO **)csoundQueryGlobalVariable(csound, "_MODULES");
  if (UNLIKELY(modules[no] == NULL || no >= MAX_MODULES))
    return CSOUND_ERROR;
  *module = modules[no]->module;
  *type = modules[no]->type;
  return CSOUND_SUCCESS;
}

PUBLIC int32_t csoundLoadPlugins(CSOUND *csound, const char *dir) {
  if (dir != NULL) {
    csound->Message(csound, "loading plugins from %s\n", dir);
    int32_t err = csoundLoadAndInitModules(csound, dir);
    if (!err) {
      return CSOUND_SUCCESS;
    } else
      return err;
  } else
    return CSOUND_ERROR;
}

PUBLIC void csoundReset(CSOUND *csound) {
  int32_t i;
  OPARMS *O = csound->oparms;

  if (csound->engineStatus & CS_STATE_COMP ||
      csound->engineStatus & CS_STATE_PRE) {
    /* and reset */
    csound->Message(csound, "resetting Csound instance\n");
    reset(csound);
    /* clear compiled flag */
    csound->engineStatus |= ~(CS_STATE_COMP);
  } else {
    csoundSpinLockInit(&csound->spoutlock);
    csoundSpinLockInit(&csound->spinlock);
    csoundSpinLockInit(&csound->memlock);
    csoundSpinLockInit(&csound->spinlock1);
    if (UNLIKELY(O->odebug))
      csound->Message(csound, "init spinlocks\n");
  }

  if (msgcallback_ != NULL) {
    csoundSetMessageCallback(csound, msgcallback_);
  } else {
    csoundSetMessageCallback(csound, csoundDefaultMessageCallback);
  }
  csound->printerrormessagesflag = (void *)1234;
  /* copysystem environment variables */
  i = csoundInitEnv(csound);
  if (UNLIKELY(i != CSOUND_SUCCESS)) {
    csound->engineStatus |= CS_STATE_JMP;
    csound->Die(csound, Str("Failed during csoundInitEnv"));
  }
  csound_init_rand(csound);
  csound->engineState.stringPool = cs_hash_table_create(csound);
  csound->engineState.constantsPool = cs_hash_table_create(csound);
  csound->engineStatus |= CS_STATE_PRE;
  csound_aops_init_tables(csound);
  create_opcode_table(csound);
  /* now load and pre-initialise external modules for this instance */
  /* this function returns an error value that may be worth checking */
  {
    int32_t err;
#ifndef BUILD_PLUGINS
    err = csoundInitStaticModules(csound);
    if (csound->delayederrormessages &&
        csound->printerrormessagesflag == NULL) {
      csound->Warning(csound, "%s", csound->delayederrormessages);
      csound->Free(csound, csound->delayederrormessages);
      csound->delayederrormessages = NULL;
    }
    if (UNLIKELY(err == CSOUND_ERROR))
      csound->Die(csound, Str("Failed during csoundInitStaticModules"));
#endif
#ifndef BARE_METAL
    csoundCreateGlobalVariable(csound, "_MODULES",
                               (size_t)MAX_MODULES * sizeof(MODULE_INFO *));
    char *modules = (char *)csoundQueryGlobalVariable(csound, "_MODULES");
    memset(modules, 0, sizeof(MODULE_INFO *) * MAX_MODULES);

    err = csoundLoadModules(csound);
    if (csound->delayederrormessages &&
        csound->printerrormessagesflag == NULL) {
      csound->Warning(csound, "%s", csound->delayederrormessages);
      csound->Free(csound, csound->delayederrormessages);
      csound->delayederrormessages = NULL;
    }
    if (UNLIKELY(err != CSOUND_SUCCESS))
      csound->Die(csound, Str("Failed during csoundLoadModules"));

    if (csoundInitModules(csound) != 0)
      csound->LongJmp(csound, 1);

#endif // BARE_METAL
    init_pvsys(csound);
    /* utilities depend on this as well as orchs; may get changed by an orch */
    dbfs_init(csound, DFLT_DBFS);
    csound->csRtClock = (RTCLOCK *)csound->Calloc(csound, sizeof(RTCLOCK));
    csoundInitTimerStruct(csound->csRtClock);
    csound->engineStatus |= /*CS_STATE_COMP |*/ CS_STATE_CLN;

    /*
      this was moved to musmon();
      print_csound_version(csound);
      print_sndfile_version(csound);
    */
    /* do not know file type yet */
    O->filetyp = -1;
    csound->peakchunks = 1;
    csound->typePool = csound->Calloc(csound, sizeof(TYPE_POOL));
    csound->engineState.varPool = csoundCreateVarPool(csound);
    csoundAddStandardTypes(csound, csound->typePool);
    /* csoundLoadExternals(csound); */
  }
  int32_t max_len = 21;
  char *s;

#ifndef BARE_METAL
  /* allow selecting real time audio module */
  csoundCreateGlobalVariable(csound, "_RTAUDIO", (size_t)max_len);
  s = csoundQueryGlobalVariable(csound, "_RTAUDIO");
#ifndef LINUX
#ifdef __HAIKU__
  strcpy(s, "haiku");
#else
#ifdef __MACH__
  strcpy(s, "auhal");
#else
  strcpy(s, "PortAudio");
#endif
#endif
#else
  strcpy(s, "alsa");
#endif

  csoundCreateConfigurationVariable(csound, "rtaudio", s, CSOUNDCFG_STRING, 0,
                                    NULL, &max_len,
                                    Str("Real time audio module name"), NULL);
#endif
  /* initialise real time MIDI */
  csound->midiGlobals = (MGLOBAL *)csound->Calloc(csound, sizeof(MGLOBAL));
  csound->midiGlobals->bufp = &(csound->midiGlobals->mbuf[0]);
  csound->midiGlobals->endatp = csound->midiGlobals->bufp;
  csoundCreateGlobalVariable(csound, "_RTMIDI", (size_t)max_len);
  csound->SetMIDIDeviceListCallback(csound, midi_dev_list_dummy);
  csound->SetExternalMidiInOpenCallback(csound, DummyMidiInOpen);
  csound->SetExternalMidiReadCallback(csound, DummyMidiRead);
  csound->SetExternalMidiOutOpenCallback(csound, DummyMidiOutOpen);
  csound->SetExternalMidiWriteCallback(csound, DummyMidiWrite);

  s = csoundQueryGlobalVariable(csound, "_RTMIDI");
  strcpy(s, "null");
  if (csound->enableHostImplementedMIDIIO == 0)
#ifndef LINUX
#ifdef __HAIKU__
    strcpy(s, "haiku");
#else
    strcpy(s, "portmidi");
#endif
#else
    strcpy(s, "alsa");
#endif
  else
    strcpy(s, "hostbased");

  csoundCreateConfigurationVariable(csound, "rtmidi", s, CSOUNDCFG_STRING, 0,
                                    NULL, &max_len,
                                    Str("Real time MIDI module name"), NULL);
  max_len = 256; /* should be the same as in csoundCore.h */
  csoundCreateConfigurationVariable(
      csound, "mute_tracks", &(csound->midiGlobals->muteTrackList[0]),
      CSOUNDCFG_STRING, 0, NULL, &max_len,
      Str("Ignore events (other than tempo "
          "changes) in tracks defined by pattern"),
      NULL);
  csoundCreateConfigurationVariable(csound, "raw_controller_mode",
                                    &(csound->midiGlobals->rawControllerMode),
                                    CSOUNDCFG_BOOLEAN, 0, NULL, NULL,
                                    Str("Do not handle special MIDI controllers"
                                        " (sustain pedal etc.)"),
                                    NULL);
#ifndef BARE_METAL
  /* sound file tag options */
  max_len = 201;
  i = (max_len + 7) & (~7);
  csound->SF_id_title = (char *)csound->Calloc(csound, (size_t)i * (size_t)6);
  csoundCreateConfigurationVariable(csound, "id_title", csound->SF_id_title,
                                    CSOUNDCFG_STRING, 0, NULL, &max_len,
                                    Str("Title tag in output soundfile "
                                        "(no spaces)"),
                                    NULL);
  csound->SF_id_copyright = (char *)csound->SF_id_title + (int32_t)i;
  csoundCreateConfigurationVariable(csound, "id_copyright",
                                    csound->SF_id_copyright, CSOUNDCFG_STRING,
                                    0, NULL, &max_len,
                                    Str("Copyright tag in output soundfile"
                                        " (no spaces)"),
                                    NULL);
  csoundCreateConfigurationVariable(csound, "id_scopyright",
                                    &csound->SF_id_scopyright,
                                    CSOUNDCFG_INTEGER, 0, NULL, &max_len,
                                    Str("Short Copyright tag in"
                                        " output soundfile"),
                                    NULL);
  csound->SF_id_software = (char *)csound->SF_id_copyright + (int32_t)i;
  csoundCreateConfigurationVariable(csound, "id_software",
                                    csound->SF_id_software, CSOUNDCFG_STRING, 0,
                                    NULL, &max_len,
                                    Str("Software tag in output soundfile"
                                        " (no spaces)"),
                                    NULL);
  csound->SF_id_artist = (char *)csound->SF_id_software + (int32_t)i;
  csoundCreateConfigurationVariable(csound, "id_artist", csound->SF_id_artist,
                                    CSOUNDCFG_STRING, 0, NULL, &max_len,
                                    Str("Artist tag in output soundfile "
                                        "(no spaces)"),
                                    NULL);
  csound->SF_id_comment = (char *)csound->SF_id_artist + (int32_t)i;
  csoundCreateConfigurationVariable(csound, "id_comment", csound->SF_id_comment,
                                    CSOUNDCFG_STRING, 0, NULL, &max_len,
                                    Str("Comment tag in output soundfile"
                                        " (no spaces)"),
                                    NULL);
  csound->SF_id_date = (char *)csound->SF_id_comment + (int32_t)i;
  csoundCreateConfigurationVariable(csound, "id_date", csound->SF_id_date,
                                    CSOUNDCFG_STRING, 0, NULL, &max_len,
                                    Str("Date tag in output soundfile "
                                        "(no spaces)"),
                                    NULL);
  {
    MYFLT minValF = FL(0.0);

    csoundCreateConfigurationVariable(csound, "msg_color",
                                      &(csound->enableMsgAttr),
                                      CSOUNDCFG_BOOLEAN, 0, NULL, NULL,
                                      Str("Enable message attributes "
                                          "(colors etc.)"),
                                      NULL);
    csoundCreateConfigurationVariable(
        csound, "skip_seconds", &(csound->csoundScoreOffsetSeconds_),
        CSOUNDCFG_MYFLT, 0, &minValF, NULL,
        Str("Start score playback at the specified"
            " time, skipping earlier events"),
        NULL);
  }
  csoundCreateConfigurationVariable(csound, "ignore_csopts",
                                    &(csound->disable_csd_options),
                                    CSOUNDCFG_BOOLEAN, 0, NULL, NULL,
                                    Str("Ignore <CsOptions> in CSD files"
                                        " (default: no)"),
                                    NULL);
#endif
}

PUBLIC int32_t csoundGetDebug(CSOUND *csound) {
  return csound->oparms_.odebug;
}

PUBLIC void csoundSetDebug(CSOUND *csound, int32_t debug) {
  csound->oparms_.odebug = debug;
}

PUBLIC int32_t csoundTableLength(CSOUND *csound, int32_t table) {
  MYFLT *tablePtr;
  return csoundGetTable(csound, &tablePtr, table);
}

PUBLIC MYFLT csoundTableGet(CSOUND *csound, int32_t table, int32_t index) {
  return csound->flist[table]->ftable[index];
}

void csoundTableSetInternal(CSOUND *csound, int32_t table, int32_t index,
                            MYFLT value) {
  if (csound->oparms->realtime)
    csoundLockMutex(csound->init_pass_threadlock);
  csound->flist[table]->ftable[index] = value;
  if (csound->oparms->realtime)
    csoundUnlockMutex(csound->init_pass_threadlock);
}

void csoundTableCopyOutInternal(CSOUND *csound, int32_t table, MYFLT *ptable) {
  int32_t len;
  MYFLT *ftab;
  /* in realtime mode init pass is executed in a separate thread, so
     we need to protect it */
  if (csound->oparms->realtime)
    csoundLockMutex(csound->init_pass_threadlock);
  len = csoundGetTable(csound, &ftab, table);
  if (UNLIKELY(len > 0x00ffffff))
    len = 0x00ffffff; // As coverity is unhappy
  memcpy(ptable, ftab, (size_t)(len * sizeof(MYFLT)));
  if (csound->oparms->realtime)
    csoundUnlockMutex(csound->init_pass_threadlock);
}

void csoundTableCopyInInternal(CSOUND *csound, int32_t table, MYFLT *ptable) {
  int32_t len;
  MYFLT *ftab;
  /* in realtime mode init pass is executed in a separate thread, so
     we need to protect it */
  if (csound->oparms->realtime)
    csoundLockMutex(csound->init_pass_threadlock);
  len = csoundGetTable(csound, &ftab, table);
  if (UNLIKELY(len > 0x00ffffff))
    len = 0x00ffffff; // As coverity is unhappy
  memcpy(ftab, ptable, (size_t)(len * sizeof(MYFLT)));
  if (csound->oparms->realtime)
    csoundUnlockMutex(csound->init_pass_threadlock);
}

static int32_t csoundDoCallback_(CSOUND *csound, void *p, uint32_t type) {
  if (csound->csoundCallbacks_ != NULL) {
    CsoundCallbackEntry_t *pp;
    pp = (CsoundCallbackEntry_t *)csound->csoundCallbacks_;
    do {
      if (pp->typeMask & type) {
        int32_t retval = pp->func(pp->userData, p, type);
        if (retval != CSOUND_SUCCESS)
          return retval;
      }
      pp = pp->nxt;
    } while (pp != (CsoundCallbackEntry_t *)NULL);
  }
  return 1;
}

/**
 * Sets a callback function that will be called on keyboard
 * events. The callback is preserved on csoundReset(), and multiple
 * callbacks may be set and will be called in reverse order of
 * registration. If the same function is set again, it is only moved
 * in the list of callbacks so that it will be called first, and the
 * user data and type mask parameters are updated. 'typeMask' can be the
 * bitwise OR of callback types for which the function should be called,
 * or zero for all types.
 * Returns zero on success, CSOUND_ERROR if the specified function
 * pointer or type mask is invalid, and CSOUND_MEMORY if there is not
 * enough memory.
 *
 * The callback function takes the following arguments:
 *   void *userData
 *     the "user data" pointer, as specified when setting the callback
 *   void *p
 *     data pointer, depending on the callback type
 *   uint32_t type
 *     callback type, can be one of the following (more may be added in
 *     future versions of Csound):
 *       CSOUND_CALLBACK_KBD_EVENT
 *       CSOUND_CALLBACK_KBD_TEXT
 *         called by the sensekey opcode to fetch key codes. The data
 *         pointer is a pointer to a single value of type 'int', for
 *         returning the key code, which can be in the range 1 to 65535,
 *         or 0 if there is no keyboard event.
 *         For CSOUND_CALLBACK_KBD_EVENT, both key press and release
 *         events should be returned (with 65536 (0x10000) added to the
 *         key code in the latter case) as unshifted ASCII codes.
 *         CSOUND_CALLBACK_KBD_TEXT expects key press events only as the
 *         actual text that is typed.
 * The return value should be zero on success, negative on error, and
 * positive if the callback was ignored (for example because the type is
 * not known).
 */

PUBLIC int32_t csoundRegisterKeyboardCallback(
    CSOUND *csound, int32_t (*func)(void *userData, void *p, uint32_t type),
    void *userData, uint32_t typeMask) {
  CsoundCallbackEntry_t *pp;

  if (UNLIKELY(func == (int32_t(*)(void *, void *, uint32_t))NULL ||
               (typeMask & (~(CSOUND_CALLBACK_KBD_EVENT |
                              CSOUND_CALLBACK_KBD_TEXT))) != 0U))
    return CSOUND_ERROR;
  csoundRemoveKeyboardCallback(csound, func);
  pp = (CsoundCallbackEntry_t *)malloc(sizeof(CsoundCallbackEntry_t));
  if (UNLIKELY(pp == (CsoundCallbackEntry_t *)NULL))
    return CSOUND_MEMORY;
  pp->typeMask = (typeMask ? typeMask : 0xFFFFFFFFU);
  pp->nxt = (CsoundCallbackEntry_t *)csound->csoundCallbacks_;
  pp->userData = userData;
  pp->func = func;
  csound->csoundCallbacks_ = (void *)pp;

  return CSOUND_SUCCESS;
}

/**
 * Removes a callback previously set with csoundSetCallback().
 */

PUBLIC void csoundRemoveKeyboardCallback(CSOUND *csound,
                                         int32_t (*func)(void *, void *,
                                                         uint32_t)) {
  CsoundCallbackEntry_t *pp, *prv;

  pp = (CsoundCallbackEntry_t *)csound->csoundCallbacks_;
  prv = (CsoundCallbackEntry_t *)NULL;
  while (pp != (CsoundCallbackEntry_t *)NULL) {
    if (pp->func == func) {
      if (prv != (CsoundCallbackEntry_t *)NULL)
        prv->nxt = pp->nxt;
      else
        csound->csoundCallbacks_ = (void *)pp->nxt;
      free((void *)pp);
      return;
    }
    prv = pp;
    pp = pp->nxt;
  }
}

PUBLIC void csoundSetOpenSoundFileCallback(
    CSOUND *p,
    void *(*openSoundFileCallback)(CSOUND *, const char *, int32_t, void *)) {
  p->OpenSoundFileCallback_ = openSoundFileCallback;
}

PUBLIC void csoundSetOpenFileCallback(CSOUND *p,
                                      FILE *(*openFileCallback)(CSOUND *,
                                                                const char *,
                                                                const char *)) {
  p->OpenFileCallback_ = openFileCallback;
}

void csoundSetFileOpenCallback(CSOUND *p,
                               void (*fileOpenCallback)(CSOUND *, const char *,
                                                        int32_t, int32_t,
                                                        int32_t)) {
  p->FileOpenCallback_ = fileOpenCallback;
}

/* csoundNotifyFileOpened() should be called by plugins via
   csound->NotifyFileOpened() to let Csound know that they opened a file
   without using one of the standard mechanisms (csound->FileOpen() or
   ldmemfile2withCB()).  The notification is passed on to the host if it
   has set the FileOpen callback. */
void csoundNotifyFileOpened(CSOUND *csound, const char *pathname,
                            int32_t csFileType, int32_t writing,
                            int32_t temporary) {
  if (csound->FileOpenCallback_ != NULL)
    csound->FileOpenCallback_(csound, pathname, csFileType, writing, temporary);
  return;
}

/* -------- IV - Jan 27 2005: timer functions -------- */

#ifdef HAVE_GETTIMEOFDAY
#undef HAVE_GETTIMEOFDAY
#endif
#if defined(LINUX) || defined(__unix) || defined(__unix__) || defined(__MACH__)
#define HAVE_GETTIMEOFDAY 1
#include <sys/time.h>
#endif

/* enable use of high resolution timer (Linux/i586/GCC only) */
/* could in fact work under any x86/GCC system, but do not   */
/* know how to query the actual CPU frequency ...            */

// #define HAVE_RDTSC  1

/* ------------------------------------ */

#if defined(HAVE_RDTSC)
#if !(defined(LINUX) && defined(__GNUC__) && defined(__i386__))
#undef HAVE_RDTSC
#endif
#endif

/* hopefully cannot change during performance */
static double timeResolutionSeconds = -1.0;

/* find out CPU frequency based on /proc/cpuinfo */

static int32_t getTimeResolution(void) {
#if defined(HAVE_RDTSC)
  FILE *f;
  char buf[256];

  /* if frequency is not known yet */
  f = fopen("/proc/cpuinfo", "r");
  if (UNLIKELY(f == NULL)) {
    fprintf(stderr, Str("Cannot open /proc/cpuinfo. "
                        "Support for RDTSC is not available.\n"));
    return -1;
  }
  /* find CPU frequency */
  while (fgets(buf, 256, f) != NULL) {
    int32_t i;
    char *s = (char *)buf - 1;
    buf[255] = '\0'; /* safety */
    if (strlen(buf) < 9)
      continue; /* too short, skip */
    while (*++s != '\0')
      if (isupper(*s))
        *s = tolower(*s); /* convert to lower case */
    if (strncmp(buf, "cpu mhz", 7) != 0)
      continue;           /* check key name */
    s = strchr(buf, ':'); /* find frequency value */
    if (s == NULL)
      continue; /* invalid entry */
    do {
      s++;
    } while (isblank(*s)); /* skip white space */
    i = CS_SSCANF(s, "%lf", &timeResolutionSeconds);

    if (i < 1 || timeResolutionSeconds < 1.0) {
      timeResolutionSeconds = -1.0; /* invalid entry */
      continue;
    }
  }
  fclose(f);
  if (UNLIKELY(timeResolutionSeconds <= 0.0)) {
    fprintf(stderr, Str("No valid CPU frequency entry "
                        "was found in /proc/cpuinfo.\n"));
    return -1;
  }
  /* MHz -> seconds */
  timeResolutionSeconds = 0.000001 / timeResolutionSeconds;
#elif defined(WIN32)
  LARGE_INTEGER tmp1;
  int_least64_t tmp2;
  QueryPerformanceFrequency(&tmp1);
  tmp2 = (int_least64_t)tmp1.LowPart + ((int_least64_t)tmp1.HighPart << 32);
  timeResolutionSeconds = 1.0 / (double)tmp2;
#elif defined(HAVE_GETTIMEOFDAY)
  timeResolutionSeconds = 0.000001;
#else
  timeResolutionSeconds = 1.0;
#endif
#ifdef CHECK_TIME_RESOLUTION // BETA
  fprintf(stderr, "time resolution is %.3f ns\n",
          1.0e9 * timeResolutionSeconds);
#endif
  return 0;
}

/* function for getting real time */

static inline int_least64_t get_real_time(void) {
#if defined(HAVE_RDTSC)
  /* optimised high resolution timer for Linux/i586/GCC only */
  uint32_t l, h;
#ifndef __STRICT_ANSI__
  asm volatile("rdtsc" : "=a"(l), "=d"(h));
#else
  __asm__ volatile("rdtsc" : "=a"(l), "=d"(h));
#endif
  return ((int_least64_t)l + ((int_least64_t)h << 32));
#elif defined(WIN32)
  /* Win32: use QueryPerformanceCounter - resolution depends on system, */
  /* but is expected to be better than 1 us. GetSystemTimeAsFileTime    */
  /* seems to have much worse resolution under Win95.                   */
  LARGE_INTEGER tmp;
  QueryPerformanceCounter(&tmp);
  return ((int_least64_t)tmp.LowPart + ((int_least64_t)tmp.HighPart << 32));
#elif defined(HAVE_GETTIMEOFDAY)
  /* UNIX: use gettimeofday() - allows 1 us resolution */
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return ((int_least64_t)tv.tv_usec +
          (int_least64_t)((uint32_t)tv.tv_sec * (uint64_t)1000000));
#else
  /* other systems: use time() - allows 1 second resolution */
  return ((int_least64_t)time(NULL));
#endif
}

/* function for getting CPU time */

static inline int_least64_t get_CPU_time(void) {
  return ((int_least64_t)((uint32_t)clock()));
}

/* initialise a timer structure */

void csoundInitTimerStruct(RTCLOCK *p) {
  p->starttime_real = get_real_time();
  p->starttime_CPU = get_CPU_time();
}

/**
 * return the elapsed real time (in seconds) since the specified timer
 * structure was initialised
 */
double csoundGetRealTime(RTCLOCK *p) {
  return ((double)(get_real_time() - p->starttime_real) *
          (double)timeResolutionSeconds);
}

/**
 * return the elapsed CPU time (in seconds) since the specified timer
 * structure was initialised
 */
double csoundGetCPUTime(RTCLOCK *p) {
  return ((double)((uint32_t)get_CPU_time() - (uint32_t)p->starttime_CPU) *
          (1.0 / (double)CLOCKS_PER_SEC));
}

/* return a 32-bit unsigned integer to be used as seed from current time */

uint32_t csoundGetRandomSeedFromTime(void) {
  return (uint32_t)get_real_time();
}

/**
 * Return the size of MYFLT in bytes.
 */
int32_t csoundGetSizeOfMYFLT(void) {
  return (int32_t)sizeof(MYFLT);
}

/**
 * Return pointer to user data pointer for real time audio input.
 */
void **csoundGetRtRecordUserData(CSOUND *csound) {
  return &(csound->rtRecord_userdata);
}

/**
 * Return pointer to user data pointer for real time audio output.
 */
void **csoundGetRtPlayUserData(CSOUND *csound) {
  return &(csound->rtPlay_userdata);
}

typedef struct opcodeDeinit_s {
  void *p;
  int32_t (*func)(CSOUND *, void *);
  void *nxt;
} opcodeDeinit_t;

/**
 * Register a function to be called by csoundReset(), in reverse order
 * of registration, before unloading external modules. The function takes
 * the Csound instance pointer as the first argument, and the pointer
 * passed here as 'userData' as the second, and is expected to return zero
 * on success.
 * The return value of csoundRegisterResetCallback() is zero on success.
 */

int32_t csoundRegisterResetCallback(CSOUND *csound, void *userData,
                                    int32_t (*func)(CSOUND *, void *)) {
  resetCallback_t *dp = (resetCallback_t *)malloc(sizeof(resetCallback_t));

  if (UNLIKELY(dp == NULL))
    return CSOUND_MEMORY;
  dp->userData = userData;
  dp->func = func;
  dp->nxt = csound->reset_list;
  csound->reset_list = (void *)dp;
  return CSOUND_SUCCESS;
}

/**
 * Returns the name of the opcode of which the data structure
 * is pointed to by 'p'.
 */
char *csoundGetOpcodeName(void *p) {
  return ((OPDS *)p)->optext->t.oentry->opname;
}

/** Returns the CS_TYPE for an opcode's arg pointer */

CS_TYPE *csoundGetTypeForArg(void *argPtr) {
  char *ptr = (char *)argPtr;
  CS_TYPE *varType = *(CS_TYPE **)(ptr - CS_VAR_TYPE_OFFSET);
  return varType;
}

/**
 * Returns the number of input arguments for opcode 'p'.
 */
int32_t csoundGetInputArgCnt(void *p) {
  return (int32_t)((OPDS *)p)->optext->t.inArgCount;
}

/**
 * Returns the name of input argument 'n' (counting from 0) for opcode 'p'.
 */
char *csoundGetInputArgName(void *p, int32_t n) {
  if ((uint32_t)n >= (uint32_t)((OPDS *)p)->optext->t.inArgCount)
    return (char *)NULL;
  return (char *)((OPDS *)p)->optext->t.inlist->arg[n];
}

/**
 * Returns the number of output arguments for opcode 'p'.
 */
int32_t csoundGetOutputArgCnt(void *p) {
  return (int32_t)((OPDS *)p)->optext->t.outArgCount;
}

/**
 * Returns the name of output argument 'n' (counting from 0) for opcode 'p'.
 */
char *csoundGetOutputArgName(void *p, int32_t n) {
  if ((uint32_t)n >= (uint32_t)((OPDS *)p)->optext->t.outArgCount)
    return (char *)NULL;
  return (char *)((OPDS *)p)->optext->t.outlist->arg[n];
}

/**
 * Set release time in control periods (1 / csound->ekr second units)
 * for opcode 'p' to 'n'. If the current release time is longer than
 * the specified value, it is not changed.
 * Returns the new release time.
 */
int32_t csoundSetReleaseLength(void *p, int32_t n) {
  if (n > (int32_t)((OPDS *)p)->insdshead->xtratim)
    ((OPDS *)p)->insdshead->xtratim = n;
  return (int32_t)((OPDS *)p)->insdshead->xtratim;
}

/**
 * Set release time in seconds for opcode 'p' to 'n'.
 * If the current release time is longer than the specified value,
 * it is not changed.
 * Returns the new release time in seconds.
 */
MYFLT csoundSetReleaseLengthSeconds(void *p, MYFLT n) {
  int32_t kcnt = (int32_t)(n * ((OPDS *)p)->insdshead->csound->ekr + FL(0.5));
  if (kcnt > (int32_t)((OPDS *)p)->insdshead->xtratim)
    ((OPDS *)p)->insdshead->xtratim = kcnt;
  return ((MYFLT)((OPDS *)p)->insdshead->xtratim *
          ((OPDS *)p)->insdshead->csound->onedkr);
}

typedef struct csMsgStruct_ {
  struct csMsgStruct_ *nxt;
  int32_t attr;
  char s[1];
} csMsgStruct;

typedef struct csMsgBuffer_ {
  void *mutex_;
  csMsgStruct *firstMsg;
  csMsgStruct *lastMsg;
  int32_t msgCnt;
  char *buf;
} csMsgBuffer;

// callback for storing messages in the buffer only
static void csoundMessageBufferCallback_1_(CSOUND *csound, int32_t attr,
                                           const char *fmt, va_list args);

// callback for writing messages to the buffer, and also stdout/stderr
static void csoundMessageBufferCallback_2_(CSOUND *csound, int32_t attr,
                                           const char *fmt, va_list args);

/**
 * Creates a buffer for storing messages printed by Csound.
 * Should be called after creating a Csound instance; note that
 * the message buffer uses the host data pointer, and the buffer
 * should be freed by calling csoundDestroyMessageBuffer() before
 * deleting the Csound instance.
 * If 'toStdOut' is non-zero, the messages are also printed to
 * stdout and stderr (depending on the type of the message),
 * in addition to being stored in the buffer.
 */

void PUBLIC csoundCreateMessageBuffer(CSOUND *csound, int32_t toStdOut) {
  csMsgBuffer *pp;
  size_t nBytes;

  pp = (csMsgBuffer *)csound->message_buffer;
  if (pp) {
    csoundDestroyMessageBuffer(csound);
  }
  nBytes = sizeof(csMsgBuffer);
  if (!toStdOut) {
    nBytes += (size_t)16384;
  }
  pp = (csMsgBuffer *)malloc(nBytes);
  pp->mutex_ = csoundCreateMutex(0);
  pp->firstMsg = (csMsgStruct *)NULL;
  pp->lastMsg = (csMsgStruct *)NULL;
  pp->msgCnt = 0;
  if (!toStdOut) {
    pp->buf = (char *)pp + (int32_t)sizeof(csMsgBuffer);
    pp->buf[0] = (char)'\0';
  } else {
    pp->buf = (char *)NULL;
  }
  csound->message_buffer = (void *)pp;
  if (toStdOut) {
    csoundSetMessageCallback(csound, csoundMessageBufferCallback_2_);
  } else {
    csoundSetMessageCallback(csound, csoundMessageBufferCallback_1_);
  }
}

/**
 * Returns the first message from the buffer.
 */
#ifdef MSVC
const char PUBLIC *csoundGetFirstMessage(CSOUND *csound)
#else
const char * /*PUBLIC*/ csoundGetFirstMessage(CSOUND *csound)
#endif
{
  csMsgBuffer *pp = (csMsgBuffer *)csound->message_buffer;
  char *msg = NULL;

  if (pp && pp->msgCnt) {
    csoundLockMutex(pp->mutex_);
    if (pp->firstMsg)
      msg = &(pp->firstMsg->s[0]);
    csoundUnlockMutex(pp->mutex_);
  }
  return msg;
}

/**
 * Returns the attribute parameter (see msg_attr.h) of the first message
 * in the buffer.
 */

int32_t PUBLIC csoundGetFirstMessageAttr(CSOUND *csound) {
  csMsgBuffer *pp = (csMsgBuffer *)csound->message_buffer;
  int32_t attr = 0;

  if (pp && pp->msgCnt) {
    csoundLockMutex(pp->mutex_);
    if (pp->firstMsg) {
      attr = pp->firstMsg->attr;
    }
    csoundUnlockMutex(pp->mutex_);
  }
  return attr;
}

/**
 * Removes the first message from the buffer.
 */

void PUBLIC csoundPopFirstMessage(CSOUND *csound) {
  csMsgBuffer *pp = (csMsgBuffer *)csound->message_buffer;

  if (pp) {
    csMsgStruct *tmp;
    csoundLockMutex(pp->mutex_);
    tmp = pp->firstMsg;
    if (tmp) {
      pp->firstMsg = tmp->nxt;
      pp->msgCnt--;
      if (!pp->firstMsg)
        pp->lastMsg = (csMsgStruct *)0;
    }
    csoundUnlockMutex(pp->mutex_);
    if (tmp)
      free((void *)tmp);
  }
}

/**
 * Returns the number of pending messages in the buffer.
 */

int32_t PUBLIC csoundGetMessageCnt(CSOUND *csound) {
  csMsgBuffer *pp = (csMsgBuffer *)csound->message_buffer;
  int32_t cnt = -1;

  if (pp) {
    csoundLockMutex(pp->mutex_);
    cnt = pp->msgCnt;
    csoundUnlockMutex(pp->mutex_);
  }
  return cnt;
}

/**
 * Releases all memory used by the message buffer.
 */

void PUBLIC csoundDestroyMessageBuffer(CSOUND *csound) {
  csMsgBuffer *pp = (csMsgBuffer *)csound->message_buffer;
  if (!pp) {
    csound->Warning(csound, Str("csoundDestroyMessageBuffer: "
                                "Message buffer not allocated."));
    return;
  }
  csMsgStruct *msg = pp->firstMsg;
  while (msg) {
    csMsgStruct *tmp = msg;
    msg = tmp->nxt;
    free(tmp);
  }
  csound->message_buffer = NULL;
  csoundSetMessageCallback(csound, NULL);
  while (csoundGetMessageCnt(csound) > 0) {
    csoundPopFirstMessage(csound);
  }
  csoundSetHostData(csound, NULL);
  csoundDestroyMutex(pp->mutex_);
  free((void *)pp);
}

static void csoundMessageBufferCallback_1_(CSOUND *csound, int32_t attr,
                                           const char *fmt, va_list args) {
  csMsgBuffer *pp = (csMsgBuffer *)csound->message_buffer;
  csMsgStruct *p;
  int32_t len;

  csoundLockMutex(pp->mutex_);
  len = vsnprintf(pp->buf, 16384, fmt, args); // FIXEDME: this can overflow
  va_end(args);
  if (UNLIKELY((uint32_t)len >= (uint32_t)16384)) {
    csoundUnlockMutex(pp->mutex_);
    fprintf(stderr, Str("csound: internal error: message buffer overflow\n"));
    exit(-1);
  }
  p = (csMsgStruct *)malloc(sizeof(csMsgStruct) + (size_t)len);
  p->nxt = (csMsgStruct *)NULL;
  p->attr = attr;
  strcpy(&(p->s[0]), pp->buf);
  if (pp->firstMsg == (csMsgStruct *)0) {
    pp->firstMsg = p;
  } else {
    pp->lastMsg->nxt = p;
  }
  pp->lastMsg = p;
  pp->msgCnt++;
  csoundUnlockMutex(pp->mutex_);
}

static void csoundMessageBufferCallback_2_(CSOUND *csound, int32_t attr,
                                           const char *fmt, va_list args) {
  csMsgBuffer *pp = (csMsgBuffer *)csound->message_buffer;
  csMsgStruct *p;
  int32_t len = 0;
  va_list args_save;

  va_copy(args_save, args);
  switch (attr & CSOUNDMSG_TYPE_MASK) {
  case CSOUNDMSG_ERROR:
  case CSOUNDMSG_REALTIME:
  case CSOUNDMSG_WARNING:
    len = vfprintf(stderr, fmt, args);
    break;
  default:
    len = vfprintf(stdout, fmt, args);
  }
  va_end(args);
  p = (csMsgStruct *)malloc(sizeof(csMsgStruct) + (size_t)len);
  p->nxt = (csMsgStruct *)NULL;
  p->attr = attr;
  vsnprintf(&(p->s[0]), len, fmt, args_save);
  va_end(args_save);
  csoundLockMutex(pp->mutex_);
  if (pp->firstMsg == (csMsgStruct *)NULL)
    pp->firstMsg = p;
  else
    pp->lastMsg->nxt = p;
  pp->lastMsg = p;
  pp->msgCnt++;
  csoundUnlockMutex(pp->mutex_);
}

static INSTRTXT **csoundGetInstrumentList(CSOUND *csound) {
  return csound->engineState.instrtxtp;
}

uint64_t csoundGetKcounter(CSOUND *csound) {
  return csound->kcounter;
}

static void set_util_sr(CSOUND *csound, MYFLT sr) {
  csound->esr = sr;
}
static void set_util_nchnls(CSOUND *csound, int32_t nchnls) {
  csound->nchnls = nchnls;
}

MYFLT csoundGetA4(CSOUND *csound) {
  return (MYFLT)csound->A4;
}

int32_t csoundErrCnt(CSOUND *csound) {
  return csound->perferrcnt;
}

INSTRTXT *csoundGetInstrument(CSOUND *csound, int32_t insno, const char *name) {
  if (name != NULL)
    insno = named_instr_find(csound, (char *)name);
  return csound->engineState.instrtxtp[insno];
}


