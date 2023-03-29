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

#ifndef __FCITX_CANNA_H
#define __FCITX_CANNA_H

#include <fcitx/ime.h>
#include <fcitx/instance.h>
#include <fcitx/candidate.h>
#include <fcitx-utils/utils.h>
#include <libintl.h>
#include <iconv.h>

#define	KEYTOSTRSIZE 2048

typedef struct {
    FcitxInstance *owner;
    boolean initialized;
    iconv_t euc2utf;
    unsigned char canna_buf[KEYTOSTRSIZE];
    unsigned char last_mode[10], mode[10];
    unsigned char error[KEYTOSTRSIZE];
    unsigned char kakutei[KEYTOSTRSIZE];
    unsigned char henkan[KEYTOSTRSIZE];
    int henkan_len, henkan_revPos, henkan_revLen;
    unsigned char ichiran[KEYTOSTRSIZE];
    int ichiran_len, ichiran_revPos, ichiran_revLen;
    boolean auxdown;
} FcitxCanna;

#endif
