/*
  ios.h

  Copyright (c) 2021-2022
  https://tuxpaint.org/

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
  (See COPYING.txt)

  Last updated: December 11, 2022
*/
#ifndef __IOS_H__
#define __IOS_H__


void apple_init(void);
const char *apple_locale(void);
const char *apple_fontsPath(void);
const char *apple_preferencesPath(void);
const char *apple_globalPreferencesPath(void);
const char *apple_picturesPath(void);
int apple_trash(const char *path);


#endif /* __IOS_H__ */
