/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.              *
 ***************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcitx/ime.h>
#include <fcitx-config/fcitx-config.h>
#include <fcitx-config/xdg.h>
#include <fcitx-config/hotkey.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/keysym.h>
#include <fcitx/candidate.h>
#include <fcitx/context.h>
#include <fcitx/ui.h>
#include <fcitx/hook.h>

#define CANNA3_7
#ifdef CANNA3_7
# define CANNA_NEW_WCHAR_AWARE
#endif

#define IROHA_BC
#include "canna/jrkanji.h"
#include "canna/RK.h"

#ifndef CANNA3_7
extern char *jrKanjiError;
#endif

#include "canna.h"

#define _DEBUG_ 0
#if _DEBUG_
#include <fcntl.h>
static int _dbg_fd = -2;
static char _dbg_buf[4096];
static void
_dbg_put()
{
  if (_dbg_fd == -2) {
    _dbg_fd = open("/tmp/fcitx-canna.log", O_WRONLY|O_APPEND|O_CREAT, 0600);
  }
  if (_dbg_fd > 0)
    (void) write(_dbg_fd, _dbg_buf, strlen(_dbg_buf));
}
#endif

/* ===========================================================
 *	Canna related functions
 * ===========================================================
 */

/*
 * Canna internal code is EUC-JP, so needs code convertion
 */
static void
_canna_euc2utf(FcitxCanna *canna, unsigned char *dst, int dsize,
		unsigned char *src, int ssize)
{
    size_t dsz, ssz;
    char *dp, *sp;

    if (dst) *dst = '\0';
    if (!canna->euc2utf)
	return;
    iconv(canna->euc2utf, NULL, NULL, NULL, NULL);
    dp = (char*)dst; dsz = dsize-1;
    sp = (char*)src; ssz = ssize;
    iconv(canna->euc2utf, &sp, &ssz, &dp, &dsz);
    *dp = '\0';
}

/*
 * return Canna internal string length in character (not byte)
 */
static int
_canna_strlen(unsigned char *p, int l)
{
#define ISO_CODE_SS2 0x8E
#define ISO_CODE_SS3 0x8F
#define MULE_STR_SS2 1
#define MULE_STR_SS3 1
#define MULE_STR_KNJ 1
  unsigned char ch, *cp = p;
  int len = 0;
  
  while((cp < p + l) && (ch = *cp)) {
    if ((unsigned char)ch == ISO_CODE_SS2) {
      len += MULE_STR_SS2;
      cp += 2;
    }
    else if ((unsigned char)ch == ISO_CODE_SS3) {
      len += MULE_STR_SS3;
      cp += 3;
    }
    else if(ch & 0x80) {
      len += MULE_STR_KNJ;
      cp += 2;
    }
    else {
      len++;
      cp++;	
    }
  }
  return(len);
}

/*
 * setup 4 ui windows and commit kakutei if it exists
 *	AuxUp: error message
 *	  or   [henkan-mode]
 *	  or   [henkan-mode] ichiran
 *
 *	ClientPreedit: henkan
 *
 * this layout provides `OnTheSpot' like appearance of kinput2,
 * but assuming "Show Preedit String in Client Window" is true
 * (it is the default).
 */
static void
_canna_setup_ui_windows(FcitxCanna *canna)
{
    FcitxInputState* input = FcitxInstanceGetInputState(canna->owner);
    FcitxMessages* preedit = FcitxInputStateGetPreedit(input);
    FcitxMessages* clientpreedit = FcitxInputStateGetClientPreedit(input);
    FcitxMessages* auxup = FcitxInputStateGetAuxUp(input);
    FcitxMessages* auxdown = FcitxInputStateGetAuxDown(input);

    FcitxMessagesSetMessageCount(preedit, 0); 
    FcitxMessagesSetMessageCount(clientpreedit, 0); 
    FcitxMessagesSetMessageCount(auxup, 0); 
    FcitxMessagesSetMessageCount(auxdown, 0); 

    /* error messges goes to 'AuxUp' */
    if (canna->error[0]) {
	FcitxMessagesAddMessageAtLast(auxup, MSG_OTHER, "%s", canna->error);
	canna->error[0] = '\0';
	return;
    }

    /* commit kakutei */
    if (canna->kakutei[0]) {
	FcitxInstanceCommitString(canna->owner,
				  FcitxInstanceGetCurrentIC(canna->owner),
				  (char*)canna->kakutei);
	canna->kakutei[0] = '\0';
	return;
    }

    /* henkan-mode goes to 'AuxUp' */
    FcitxMessagesAddMessageAtLast(auxup, MSG_OTHER, "%s",
				  canna->mode[0] ? (char*)canna->mode : "canna");

    /* ichiran goes to 'AuxUp' too */
    if (canna->ichiran[0]) {
      if (canna->ichiran_revLen) {
	int ch;
	char *p, *q;
	p = fcitx_utf8_get_nth_char((char*)canna->ichiran, canna->ichiran_revPos);
	q = fcitx_utf8_get_nth_char(p, canna->ichiran_revLen);
	ch = *p; *p = '\0';
	FcitxMessagesAddMessageAtLast(auxup, MSG_OTHER, "%s", canna->ichiran);
	*p = ch;
	ch = *q; *q = '\0';
	FcitxMessagesAddMessageAtLast(auxup, MSG_CANDIATE_CURSOR, "%s", p);
	*q = ch;
	FcitxMessagesAddMessageAtLast(auxup, MSG_OTHER, "%s", q);
      } else {
	FcitxMessagesAddMessageAtLast(auxup, MSG_OTHER, "%s", canna->ichiran);
      }
    }
    /* henkan goes to 'ClientPreedit' */
    if (canna->henkan_revLen) {
      int ch;
      char *p, *q;
      p = fcitx_utf8_get_nth_char((char*)canna->henkan, canna->henkan_revPos);
      q = fcitx_utf8_get_nth_char(p, canna->henkan_revLen);
      ch = *p; *p = '\0';
      FcitxMessagesAddMessageAtLast(clientpreedit, MSG_OTHER, "%s", canna->henkan);
      *p = ch;
      ch = *q; *q = '\0';
      FcitxMessagesAddMessageAtLast(clientpreedit, MSG_HIGHLIGHT, "%s", p);
      *q = ch;
      FcitxMessagesAddMessageAtLast(clientpreedit, MSG_OTHER, "%s", q);
      FcitxInputStateSetClientCursorPos(input, (int)(p-(char*)canna->henkan));
    } else {
      FcitxMessagesAddMessageAtLast(clientpreedit, MSG_OTHER, "%s", canna->henkan);
      FcitxInputStateSetClientCursorPos(input, strlen((char*)canna->henkan));
    }
}

/*
 * setup kakutei, henkan, ichiran strings from the result
 */
static void
_canna_storeResults(FcitxCanna *canna, unsigned char *buf, int len, jrKanjiStatus *ks)
{
  /* clear result area */
  canna->error[0] = '\0';
  canna->kakutei[0] = '\0';
  canna->henkan[0] = '\0';
  canna->henkan_len = canna->henkan_revPos = canna->henkan_revLen = 0;
  canna->ichiran[0] = '\0';
  canna->ichiran_len = canna->ichiran_revPos = canna->ichiran_revLen = 0;


  if (len < 0) { /* Error detected */
    _canna_euc2utf(canna, canna->error, sizeof(canna->error),
		    (unsigned char*)jrKanjiError, strlen(jrKanjiError));
#if _DEBUG_
    snprintf(_dbg_buf, sizeof(_dbg_buf),
	     "    error:%s\n", canna->error);
    _dbg_put();
#endif
  } else {
    /* converted string */
    _canna_euc2utf(canna, canna->kakutei, sizeof(canna->kakutei), buf, len);
#if _DEBUG_
    snprintf(_dbg_buf, sizeof(_dbg_buf),
	     "    kakutei:%ld:%s\n",
	     strlen((char*)canna->kakutei), canna->kakutei);
    _dbg_put();
#endif

    /* candidate strings */
    if (ks->length >= 0) {
      _canna_euc2utf(canna, canna->henkan, sizeof(canna->henkan),
		      ks->echoStr, ks->length);
      canna->henkan_len = _canna_strlen(ks->echoStr,ks->length);
      canna->henkan_revPos = _canna_strlen(ks->echoStr,ks->revPos);
      canna->henkan_revLen = _canna_strlen(ks->echoStr+ks->revPos,ks->revLen);
#if _DEBUG_
      snprintf(_dbg_buf, sizeof(_dbg_buf),
	       "    henkan:%d:%d:%d:%s\n",
	       canna->henkan_len, canna->henkan_revPos, canna->henkan_revLen,
	       canna->henkan);
      _dbg_put();
#endif
    }

    /* listing infomation */
    if (ks->info & KanjiGLineInfo && ks->gline.length >= 0) {
      _canna_euc2utf(canna, canna->ichiran, sizeof(canna->ichiran),
		      ks->gline.line, ks->gline.length);
      canna->ichiran_len = _canna_strlen(ks->gline.line,ks->gline.length);
      canna->ichiran_revPos = _canna_strlen(ks->gline.line,ks->gline.revPos);
      canna->ichiran_revLen = _canna_strlen(ks->gline.line+ks->gline.revPos,ks->gline.revLen);
#if _DEBUG_
      snprintf(_dbg_buf, sizeof(_dbg_buf),
	       "    ichiran:%d:%d:%d:%s\n",
	       canna->ichiran_len, canna->ichiran_revPos, canna->ichiran_revLen,
	       canna->ichiran);
      _dbg_put();
#endif
    }

    /* conversion mode */
    if (ks->info & KanjiModeInfo) {
      _canna_euc2utf(canna, canna->mode, sizeof(canna->mode),
		      ks->mode, strlen((char*)ks->mode));
#if _DEBUG_
      snprintf(_dbg_buf, sizeof(_dbg_buf),
	       "    mode:%s\n", canna->mode);
      _dbg_put();
#endif
    }
    if (canna->mode[0] == '\0')
      strcpy((char*)canna->mode, "canna");
  }
}

static boolean
_canna_connect_server(FcitxCanna *canna)
{
  if (!canna->initialized) {
#if _DEBUG_
    snprintf(_dbg_buf, sizeof(_dbg_buf),
	     "    connect to canna server\n");
    _dbg_put();
#endif
    int kugiri = 1; /* separete words */

    jrKanjiControl(0, KC_SETSERVERNAME, (char *)NULL);
    jrKanjiControl(0, KC_SETINITFILENAME, (char *)NULL);
    if (jrKanjiControl(0, KC_INITIALIZE, (char *)NULL) < 0)
      return false;

    wcKanjiControl(0, KC_SETAPPNAME, "fcitx");

    jrKanjiControl(0, KC_SETBUNSETSUKUGIRI, (char *)((long)kugiri));
    jrKanjiControl(0, KC_SETWIDTH, (char *)78);
    jrKanjiControl(0, KC_INHIBITHANKAKUKANA, (char *)0);
    jrKanjiControl(0, KC_YOMIINFO, (char *)2); /* back to Roman */
    canna->initialized = true;
  }
  return canna->initialized;
}

static void
_canna_disconnect_server(FcitxCanna *canna)
{
  if (canna->initialized) {
#if _DEBUG_
    snprintf(_dbg_buf, sizeof(_dbg_buf),
	     "    connect to canna server\n");
    _dbg_put();
#endif
    jrKanjiControl(0, KC_FINALIZE, (char *)NULL);
    canna->initialized = false;
  }
}

static void
_canna_reset_input_mode(FcitxCanna *canna)
{
    if (!_canna_connect_server(canna))
	return;

    jrKanjiStatusWithValue ksv;
    jrKanjiStatus ks;

    ksv.buffer = canna->canna_buf;
    ksv.bytes_buffer = sizeof(canna->canna_buf);
    ksv.ks = &ks;
    ksv.val = CANNA_MODE_EmptyMode;
    jrKanjiControl(0, KC_CHANGEMODE,  (char *)&ksv);
    _canna_storeResults(canna, canna->canna_buf, ksv.val, ksv.ks);
}

static int
_fcitxkey_to_canna(FcitxKeySym sym, unsigned int state)
{
    struct _f2c_ {
	FcitxKeySym fkey;
	int ckey;
    } f2c [] = {
	{FcitxKey_BackSpace,	0x08},
	{FcitxKey_Tab,	0x09},
	{FcitxKey_Linefeed,	0x0a},
	{FcitxKey_Return,	0x0d},
	{FcitxKey_Escape,	0x1b},
	{FcitxKey_Delete,	0x7f},
	{FcitxKey_Home,	CANNA_KEY_Home},
	{FcitxKey_Left,	CANNA_KEY_Left},
	{FcitxKey_Up,	CANNA_KEY_Up},
	{FcitxKey_Right,	CANNA_KEY_Right},
	{FcitxKey_Down,	CANNA_KEY_Down},
	{FcitxKey_Page_Up,	CANNA_KEY_PageUp},
	{FcitxKey_Page_Down,	CANNA_KEY_PageDown},
	{FcitxKey_End,	CANNA_KEY_End},
	{FcitxKey_KP_F1,	CANNA_KEY_PF1},
	{FcitxKey_KP_F2,	CANNA_KEY_PF2},
	{FcitxKey_KP_F3,	CANNA_KEY_PF3},
	{FcitxKey_KP_F4,	CANNA_KEY_PF4},
	{FcitxKey_KP_Separator,	CANNA_KEY_KP_Separator},
	{FcitxKey_KP_Subtract,	CANNA_KEY_KP_Subtract},
	{FcitxKey_KP_Decimal,	CANNA_KEY_KP_Decimal },
	{FcitxKey_KP_Divide,	CANNA_KEY_KP_Divide },
	{FcitxKey_F1,	CANNA_KEY_F1},
	{FcitxKey_F2,	CANNA_KEY_F2},
	{FcitxKey_F3,	CANNA_KEY_F3},
	{FcitxKey_F4,	CANNA_KEY_F4},
	{FcitxKey_F5,	CANNA_KEY_F5},
	{FcitxKey_F6,	CANNA_KEY_F6},
	{FcitxKey_F7,	CANNA_KEY_F7},
	{FcitxKey_F8,	CANNA_KEY_F8},
	{FcitxKey_F9,	CANNA_KEY_F9},
	{FcitxKey_F10,	CANNA_KEY_F10},
	{0,0}
    };
    int i;

    if ((sym & ~0x7f) == 0) {
	if (state == FcitxKeyState_None || state == FcitxKeyState_Shift) {
	    /* normal ascii printable character */
	    return (int)sym;
	}
	if (state == FcitxKeyState_Ctrl) {
	    /* normal ascii control character */
	    return (int)sym & 0x1f;
	}
    }
    /* map special key symbols */
    for (i=0; f2c[i].fkey; i++) {
	if (f2c[i].fkey == sym) {
	    return f2c[i].ckey;
	}
    }

    return -1;
}

/* send a key to the canna server and receive response */
static INPUT_RETURN_VALUE
_canna_process_key(FcitxCanna *canna, FcitxKeySym sym, unsigned int state)
{
    int key = _fcitxkey_to_canna(sym, state);
#if _DEBUG_
    snprintf(_dbg_buf, sizeof(_dbg_buf),
	     "_canna_process_key(0x%lx,0x%lx,0x%lx) 0x%x\n",
	     (unsigned long)(canna->owner),
	     (unsigned long)sym,
	     (unsigned long)state,
	     key);
    _dbg_put();
#endif

    while (key>=0) {
	jrKanjiStatus ks;
	int len;
	boolean was_idle = canna->henkan[0] == '\0';

	len = jrKanjiString(0, key, (char*)canna->canna_buf, KEYTOSTRSIZE, &ks);
	_canna_storeResults(canna, canna->canna_buf, len, &ks);
	if (was_idle && canna->henkan[0] == '\0') {
	    break;
	}
	_canna_setup_ui_windows(canna);
        return IRV_DISPLAY_MESSAGE | IRV_DO_NOTHING;
    }
#if _DEBUG_
    snprintf(_dbg_buf, sizeof(_dbg_buf),
	     "    key(0x%x) is not consumed\n", key);
    _dbg_put();
#endif
    return IRV_TO_PROCESS;
}

static void
_canna_exit()
{
#if _DEBUG_
    snprintf(_dbg_buf, sizeof(_dbg_buf), "_canna_exit()\n");
    _dbg_put();
#endif
    /* noop */
}

/*
 * =============================================================
 *	functions to register fcitx addon &
 *	to implement input method for Canna server
 * =============================================================
 */
static void *FcitxCannaCreate(FcitxInstance *instance);
static void FcitxCannaDestroy(void *arg);
static void FcitxCannaReloadConfig(void *arg);

/* addon entry */
FCITX_DEFINE_PLUGIN(fcitx_canna, ime2, FcitxIMClass2) = {
    FcitxCannaCreate,
    FcitxCannaDestroy,
    FcitxCannaReloadConfig,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

static boolean FcitxCannaInit(void *arg);
static INPUT_RETURN_VALUE FcitxCannaDoInput(void *arg, FcitxKeySym sym,
					    unsigned int state);
static void FcitxCannaReset(void *arg);

/* input method interface */
static const FcitxIMIFace canna_iface = {
    .ResetIM = FcitxCannaReset,
    .DoInput = FcitxCannaDoInput,
    .GetCandWords = NULL,
    .PhraseTips = NULL,
    .Save = NULL,
    .Init = FcitxCannaInit,
    .ReloadConfig = NULL,
    .KeyBlocker = NULL,
    .UpdateSurroundingText = NULL,
    .DoReleaseInput = NULL,
    .OnClose = NULL,
    .GetSubModeName = NULL
};

void FcitxCannaResetHook(void *arg)
{
    FcitxCanna *canna = (FcitxCanna*)arg;
#if _DEBUG_
    snprintf(_dbg_buf, sizeof(_dbg_buf),
	     "FcitxCannaResetHook(0x%lx)\n", (unsigned long)(canna->owner));
    _dbg_put();
#endif
    FcitxCannaReset(arg);
}

static boolean
FcitxCannaInit(void *arg)
{
    FcitxCanna *canna = (FcitxCanna*)arg;
#if _DEBUG_
    snprintf(_dbg_buf, sizeof(_dbg_buf),
	     "FcitxCannaInit(0x%lx)\n", (unsigned long)(canna->owner));
    _dbg_put();
#endif
    if (!arg)
        return false;

    FcitxInstanceSetContext(canna->owner, CONTEXT_IM_KEYBOARD_LAYOUT, "ja");
    boolean flag = true;
    FcitxInstanceSetContext(canna->owner, CONTEXT_IM_KEYBOARD_LAYOUT, "jp");
    FcitxInstanceSetContext(canna->owner, CONTEXT_DISABLE_AUTOENG, &flag);
    FcitxInstanceSetContext(canna->owner, CONTEXT_DISABLE_QUICKPHRASE, &flag);
    FcitxInstanceSetContext(canna->owner, CONTEXT_DISABLE_FULLWIDTH, &flag);
    FcitxInstanceSetContext(canna->owner, CONTEXT_DISABLE_AUTO_FIRST_CANDIDATE_HIGHTLIGHT, &flag);
    if (_canna_connect_server(canna)) {
#if _DEBUG_
	snprintf(_dbg_buf, sizeof(_dbg_buf),
		 "    reset canna state\n");
	_dbg_put();
#endif
    }
    return canna->initialized;
}

static INPUT_RETURN_VALUE
FcitxCannaDoInput(void *arg, FcitxKeySym sym, unsigned int state)
{
    FcitxCanna *canna = (FcitxCanna*)arg;
#if _DEBUG_
    snprintf(_dbg_buf, sizeof(_dbg_buf),
	     "FcitxCannaDoInput(0x%lx,0x%lx,0x%lx)\n",
	     (unsigned long)(canna->owner),
	     (unsigned long)sym,
	     (unsigned long)state);
    _dbg_put();
#endif
    FcitxInputState* input = FcitxInstanceGetInputState(canna->owner);
    sym = FcitxInputStateGetKeySym(input);
#if _DEBUG_
    snprintf(_dbg_buf, sizeof(_dbg_buf),
	     "    FcitxCannaDoInput().sym = 0x%lx;\n" ,(unsigned long)sym);
    _dbg_put();
#endif
    state = FcitxInputStateGetKeyState(input);
#if _DEBUG_
    snprintf(_dbg_buf, sizeof(_dbg_buf),
	     "    FcitxCannaDoInput().state = 0x%lx;\n" ,(unsigned long)state);
    _dbg_put();
#endif
    return _canna_process_key(canna, sym, state);
}

static void
FcitxCannaReset(void *arg)
{
    FcitxCanna *canna = (FcitxCanna*)arg;
#if _DEBUG_
    snprintf(_dbg_buf, sizeof(_dbg_buf),
	     "FcitxCannaReset(0x%lx)\n", (unsigned long)(canna->owner));
    _dbg_put();
#endif
    if (canna->initialized) {
#if _DEBUG_
      snprintf(_dbg_buf, sizeof(_dbg_buf),
	       "    reset canna state\n");
      _dbg_put();
#endif
    }
    _canna_reset_input_mode(canna);
}

static void*
FcitxCannaCreate(FcitxInstance *instance)
{
#if _DEBUG_
    snprintf(_dbg_buf, sizeof(_dbg_buf),
	     "FcitxCannaCreate(0x%lx)\n", (unsigned long)instance);
    _dbg_put();
#endif
/*  no canna related directories under fcitx
    bindtextdomain("fcitx-canna", LOCALEDIR);
    bind_textdomain_codeset("fcitx-canna", "UTF-8"); */

    FcitxCanna *canna = fcitx_utils_new(FcitxCanna);
    canna->owner = instance;
    canna->initialized = false;
    canna->euc2utf = iconv_open("UTF-8", "EUC-JP"); /* (dst, src) */

    (void) atexit(_canna_exit);

    FcitxInstanceRegisterIMv2(instance, canna, "canna", "Canna", "canna",
                              canna_iface, 1, "ja");

    FcitxIMEventHook hk;
    hk.arg = canna;
    hk.func = FcitxCannaResetHook;
    FcitxInstanceRegisterResetInputHook(instance, hk);

    return canna;
}

static void
FcitxCannaDestroy(void *arg)
{
    FcitxCanna *canna = (FcitxCanna*)arg;
#if _DEBUG_
    snprintf(_dbg_buf, sizeof(_dbg_buf),
	     "FcitxCannaDestroy(0x%lx)\n", (unsigned long)(canna->owner));
    _dbg_put();
#endif
    _canna_disconnect_server(canna);

    if (canna->euc2utf)
	(void) iconv_close(canna->euc2utf);

    if (fcitx_unlikely(!arg))
        return;
    free(arg);
}

static void
FcitxCannaReloadConfig(void *arg)
{
    FcitxCanna *canna = (FcitxCanna*)arg;
#if _DEBUG_
    snprintf(_dbg_buf, sizeof(_dbg_buf),
	     "FcitxCannaReloadConfig(0x%lx)\n", (unsigned long)(canna->owner));
    _dbg_put();
#endif
    /* noop */
}
