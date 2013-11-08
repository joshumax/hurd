/* unicode.h - A list of useful Unicode characters.
   Copyright (C) 2002 Free Software Foundation, Inc.
   Written by Marcus Brinkmann.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#ifndef _UNICODE_H_
#define _UNICODE_H_

#define UNICODE_NO_BREAK_SPACE				     ((wchar_t) 0x00a0)
#define UNICODE_INVERTED_EXCLAMATION_MARK		     ((wchar_t) 0x00a1)
#define UNICODE_CENT_SIGN				     ((wchar_t) 0x00a2)
#define UNICODE_POUND_SIGN				     ((wchar_t) 0x00a3)
#define UNICODE_CURRENCY_SIGN				     ((wchar_t) 0x00a4)
#define UNICODE_YEN_SIGN				     ((wchar_t) 0x00a5)
#define UNICODE_BROKEN_BAR				     ((wchar_t) 0x00a6)
#define UNICODE_BROKEN_VERTICAL_BAR UNICODE_BROKEN_BAR
#define UNICODE_SECTION_SIGN				     ((wchar_t) 0x00a7)
#define UNICODE_DIARESIS				     ((wchar_t) 0x00a8)
#define UNICODE_COPYRIGHT_SIGN				     ((wchar_t) 0x00a9)
#define UNICODE_FEMININE_ORDINAL_INDICATOR		     ((wchar_t) 0x00aa)
#define UNICODE_LEFT_POINTING_DOUBLE_ANGLE_QUOTATION_MARK    ((wchar_t) 0x00ab)
#define UNICODE_LEFT_POINTING_GUILLEMET \
  UNICODE_LEFT_POINTING_DOUBLE_ANGLE_QUOTATION_MARK
#define UNICODE_NOT_SIGN				     ((wchar_t) 0x00ac)
#define UNICODE_SOFT_HYPHEN				     ((wchar_t) 0x00ad)
#define UNICODE_REGISTERED_SIGN				     ((wchar_t) 0x00ae)
#define UNICODE_REGISTERED_TRADE_MARK_SIGN UNICODE_REGISTERED_SIGN
#define UNICODE_MACRON					     ((wchar_t) 0x00af)
#define UNICODE_DEGREE_SIGN				     ((wchar_t) 0x00b0)
#define UNICODE_PLUS_MINUS_SIGN				     ((wchar_t) 0x00b1)
#define UNICODE_SUPERSCRIPT_TWO				     ((wchar_t) 0x00b2)
#define UNICODE_SUPERSCRIPT_THREE			     ((wchar_t) 0x00b3)
#define UNICODE_ACUTE_ACCENT				     ((wchar_t) 0x00b4)
#define UNICODE_MICRO_SIGN				     ((wchar_t) 0x00b5)
#define UNICODE_PILCROW_SIGN				     ((wchar_t) 0x00b6)
#define UNICODE_PARAGRAPH_SIGN UNICODE_PILCROW_SIGN
#define UNICODE_MIDDLE_DOT				     ((wchar_t) 0x00b7)
#define UNICODE_CEDILLA					     ((wchar_t) 0x00b8)
#define UNICODE_SUPERSCRIPT_ONE				     ((wchar_t) 0x00b9)
#define UNICODE_MASCULINE_ORDINAL_INDICATOR		     ((wchar_t) 0x00ba)
#define UNICODE_RIGHT_POINTING_DOUBLE_ANGLE_QUOTATION_MARK   ((wchar_t) 0x00bb)
#define UNICODE_RIGHT_POINTING_GUILLEMET \
  UNICODE_RIGHT_POINTING_DOUBLE_ANGLE_QUOTATION_MARK
#define UNICODE_VULGAR_FRACTION_ONE_QUARTER		     ((wchar_t) 0x00bc)
#define UNICODE_VULGAR_FRACTION_ONE_HALF		     ((wchar_t) 0x00bd)
#define UNICODE_VULGAR_FRACTION_THREE_QUARTERS		     ((wchar_t) 0x00be)
#define UNICODE_INVERTED_QUESTION_MARK			     ((wchar_t) 0x00bf)
#define UNICODE_LATIN_CAPITAL_LETTER_A_WITH_GRAVE	     ((wchar_t) 0x00c0)
#define UNICODE_LATIN_CAPITAL_LETTER_A_WITH_ACUTE	     ((wchar_t) 0x00c1)
#define UNICODE_LATIN_CAPITAL_LETTER_A_WITH_CIRCUMFLEX	     ((wchar_t) 0x00c2)
#define UNICODE_LATIN_CAPITAL_LETTER_A_WITH_TILDE	     ((wchar_t) 0x00c3)
#define UNICODE_LATIN_CAPITAL_LETTER_A_WITH_DIARESIS	     ((wchar_t) 0x00c4)
#define UNICODE_LATIN_CAPITAL_LETTER_A_WITH_RING_ABOVE	     ((wchar_t) 0x00c5)
#define UNICODE_LATIN_CAPITAL_LETTER_AE			     ((wchar_t) 0x00c6)
#define UNICODE_LATIN_CAPITAL_LETTER_C_WITH_CEDILLA	     ((wchar_t) 0x00c7)
#define UNICODE_LATIN_CAPITAL_LIGATURE_AE UNICODE_LATIN_CAPITAL_LETTER_AE
#define UNICODE_LATIN_CAPITAL_LETTER_E_WITH_GRAVE	     ((wchar_t) 0x00c8)
#define UNICODE_LATIN_CAPITAL_LETTER_E_WITH_ACUTE	     ((wchar_t) 0x00c9)
#define UNICODE_LATIN_CAPITAL_LETTER_E_WITH_CIRCUMFLEX	     ((wchar_t) 0x00ca)
#define UNICODE_LATIN_CAPITAL_LETTER_E_WITH_DIARESIS	     ((wchar_t) 0x00cb)
#define UNICODE_LATIN_CAPITAL_LETTER_I_WITH_GRAVE	     ((wchar_t) 0x00cc)
#define UNICODE_LATIN_CAPITAL_LETTER_I_WITH_ACUTE	     ((wchar_t) 0x00cd)
#define UNICODE_LATIN_CAPITAL_LETTER_I_WITH_CIRCUMFLEX	     ((wchar_t) 0x00ce)
#define UNICODE_LATIN_CAPITAL_LETTER_I_WITH_DIARESIS	     ((wchar_t) 0x00cf)
#define UNICODE_LATIN_CAPITAL_LETTER_ETH		     ((wchar_t) 0x00d0)
#define UNICODE_LATIN_CAPITAL_LETTER_N_WITH_TILDE	     ((wchar_t) 0x00d1)
#define UNICODE_LATIN_CAPITAL_LETTER_O_WITH_GRAVE	     ((wchar_t) 0x00d2)
#define UNICODE_LATIN_CAPITAL_LETTER_O_WITH_ACUTE	     ((wchar_t) 0x00d3)
#define UNICODE_LATIN_CAPITAL_LETTER_O_WITH_CIRCUMFLEX	     ((wchar_t) 0x00d4)
#define UNICODE_LATIN_CAPITAL_LETTER_O_WITH_TILDE	     ((wchar_t) 0x00d5)
#define UNICODE_LATIN_CAPITAL_LETTER_O_WITH_DIARESIS	     ((wchar_t) 0x00d6)
#define UNICODE_MULTIPLICATION_SIGN			     ((wchar_t) 0x00d7)
#define UNICODE_CAPITAL_LETTER_O_WITH_STROKE		     ((wchar_t) 0x00d8)
#define UNICODE_LATIN_CAPITAL_LETTER_U_WITH_GRAVE	     ((wchar_t) 0x00d9)
#define UNICODE_LATIN_CAPITAL_LETTER_U_WITH_ACUTE	     ((wchar_t) 0x00da)
#define UNICODE_LATIN_CAPITAL_LETTER_U_WITH_CIRCUMFLEX	     ((wchar_t) 0x00db)
#define UNICODE_LATIN_CAPITAL_LETTER_U_WITH_DIARESIS	     ((wchar_t) 0x00dc)
#define UNICODE_LATIN_CAPITAL_LETTER_Y_WITH_ACUTE	     ((wchar_t) 0x00dd)
#define UNICODE_LATIN_CAPITAL_LETTER_THORN		     ((wchar_t) 0x00de)
#define UNICODE_LATIN_SMALL_LETTER_SHARP_S		     ((wchar_t) 0x00df)
#define UNICODE_LATIN_SMALL_LETTER_A_WITH_GRAVE		     ((wchar_t) 0x00e0)
#define UNICODE_LATIN_SMALL_LETTER_A_WITH_ACUTE		     ((wchar_t) 0x00e1)
#define UNICODE_LATIN_SMALL_LETTER_A_WITH_CIRCUMFLEX	     ((wchar_t) 0x00e2)
#define UNICODE_LATIN_SMALL_LETTER_A_WITH_DIARESIS	     ((wchar_t) 0x00e4)
#define UNICODE_LATIN_SMALL_LETTER_A_WITH_RING_ABOVE	     ((wchar_t) 0x00e5)
#define UNICODE_LATIN_SMALL_LETTER_AE			     ((wchar_t) 0x00e6)
#define UNICODE_LATIN_SMALL_LIGATURE_AE UNICODE_LATIN_SMALL_LETTER_AE
#define UNICODE_LATIN_SMALL_LETTER_C_WITH_CEDILLA	     ((wchar_t) 0x00e7)
#define UNICODE_LATIN_SMALL_LETTER_E_WITH_GRAVE		     ((wchar_t) 0x00e8)
#define UNICODE_LATIN_SMALL_LETTER_E_WITH_ACUTE		     ((wchar_t) 0x00e9)
#define UNICODE_LATIN_SMALL_LETTER_E_WITH_CIRCUMFLEX	     ((wchar_t) 0x00ea)
#define UNICODE_LATIN_SMALL_LETTER_E_WITH_DIARESIS	     ((wchar_t) 0x00eb)
#define UNICODE_LATIN_SMALL_LETTER_I_WITH_GRAVE		     ((wchar_t) 0x00ec)
#define UNICODE_LATIN_SMALL_LETTER_I_WITH_ACUTE		     ((wchar_t) 0x00ed)
#define UNICODE_LATIN_SMALL_LETTER_I_WITH_CIRCUMFLEX	     ((wchar_t) 0x00ee)
#define UNICODE_LATIN_SMALL_LETTER_I_WITH_DIARESIS	     ((wchar_t) 0x00ef)
#define UNICODE_LATIN_SMALL_LETTER_ETH			     ((wchar_t) 0x00f0)
#define UNICODE_LATIN_SMALL_LETTER_N_WITH_TILDE		     ((wchar_t) 0x00f1)
#define UNICODE_LATIN_SMALL_LETTER_O_WITH_GRAVE		     ((wchar_t) 0x00f2)
#define UNICODE_LATIN_SMALL_LETTER_O_WITH_ACUTE		     ((wchar_t) 0x00f3)
#define UNICODE_LATIN_SMALL_LETTER_O_WITH_CIRCUMFLEX	     ((wchar_t) 0x00f4)
#define UNICODE_LATIN_SMALL_LETTER_O_WITH_DIARESIS	     ((wchar_t) 0x00f6)
#define UNICODE_DIVISION_SIGN				     ((wchar_t) 0x00f7)
#define UNICODE_SMALL_LETTER_O_WITH_STROKE		     ((wchar_t) 0x00f8)
#define UNICODE_LATIN_SMALL_LETTER_U_WITH_GRAVE		     ((wchar_t) 0x00f9)
#define UNICODE_LATIN_SMALL_LETTER_U_WITH_ACUTE		     ((wchar_t) 0x00fa)
#define UNICODE_LATIN_SMALL_LETTER_U_WITH_CIRCUMFLEX	     ((wchar_t) 0x00fb)
#define UNICODE_LATIN_SMALL_LETTER_U_WITH_DIARESIS	     ((wchar_t) 0x00fc)
#define UNICODE_LATIN_SMALL_LETTER_Y_WITH_ACUTE		     ((wchar_t) 0x00fd)
#define UNICODE_LATIN_SMALL_LETTER_THORN		     ((wchar_t) 0x00fe)
#define UNICODE_LATIN_SMALL_LETTER_Y_WITH_DIARESIS	     ((wchar_t) 0x00ff)

#define UNICODE_LATIN_SMALL_LETTER_F_WITH_HOOK		     ((wchar_t) 0x0192)
#define UNICODE_LATIN_SMALL_LETTER_SCRIPT_F \
  UNICODE_LATIN_SMALL_LETTER_F_WITH_HOOK
#define UNICODE_GREEK_CAPITAL_LETTER_GAMMA		     ((wchar_t) 0x0393)
#define UNICODE_GREEK_CAPITAL_LETTER_OMICRON		     ((wchar_t) 0x039f)
#define UNICODE_GREEK_CAPITAL_LETTER_SIGMA		     ((wchar_t) 0x03a3)
#define UNICODE_GREEK_CAPITAL_LETTER_PHI		     ((wchar_t) 0x03a6)
#define UNICODE_GREEK_CAPITAL_LETTER_OMEGA		     ((wchar_t) 0x03a9)
#define UNICODE_GREEK_SMALL_LETTER_ALPHA		     ((wchar_t) 0x03b1)
#define UNICODE_GREEK_SMALL_LETTER_BETA			     ((wchar_t) 0x03b2)
#define UNICODE_GREEK_SMALL_LETTER_DELTA		     ((wchar_t) 0x03b4)
#define UNICODE_GREEK_SMALL_LETTER_EPSILON		     ((wchar_t) 0x03b5)
#define UNICODE_GREEK_SMALL_LETTER_MU			     ((wchar_t) 0x03bc)
#define UNICODE_GREEK_SMALL_LETTER_PI			     ((wchar_t) 0x03c0)
#define UNICODE_GREEK_SMALL_LETTER_SIGMA		     ((wchar_t) 0x03c3)
#define UNICODE_GREEK_SMALL_LETTER_TAU			     ((wchar_t) 0x03c4)
#define UNICODE_GREEK_SMALL_LETTER_PHI			     ((wchar_t) 0x03c6)

#define UNICODE_BULLET					     ((wchar_t) 0x2022)
#define UNICODE_DOUBLE_EXCLAMATION_MARK			     ((wchar_t) 0x203c)
#define UNICODE_SUPERSCRIPT_LATIN_SMALL_LETTER		     ((wchar_t) 0x207f)
#define UNICODE_PESETA_SIGN				     ((wchar_t) 0x20a7)

#define UNICODE_LEFTWARDS_ARROW				     ((wchar_t) 0x2190)
#define UNICODE_UPWARDS_ARROW				     ((wchar_t) 0x2191)
#define UNICODE_RIGHTWARDS_ARROW			     ((wchar_t) 0x2192)
#define UNICODE_DOWNWARDS_ARROW				     ((wchar_t) 0x2193)
#define UNICODE_LEFT_RIGHT_ARROW			     ((wchar_t) 0x2194)
#define UNICODE_UP_DOWN_ARROW				     ((wchar_t) 0x2195)
#define UNICODE_UP_DOWN_ARROW_WITH_BASE			     ((wchar_t) 0x21a8)

#define UNICODE_BULLET_OPERATOR				     ((wchar_t) 0x2219)
#define UNICODE_SQUARE_ROOT				     ((wchar_t) 0x221a)
#define UNICODE_INFINITY				     ((wchar_t) 0x221e)
#define UNICODE_RIGHT_ANGLE				     ((wchar_t) 0x221f)
#define UNICODE_INTERSECTION				     ((wchar_t) 0x2229)
#define UNICODE_ALMOST_EQUAL_TO				     ((wchar_t) 0x2248)
#define UNICODE_NOT_EQUAL_TO				     ((wchar_t) 0x2260)
#define UNICODE_IDENTICAL_TO				     ((wchar_t) 0x2261)
#define UNICODE_LESS_THAN_OR_EQUAL_TO			     ((wchar_t) 0x2264)
#define UNICODE_GREATER_THAN_OR_EQUAL_TO		     ((wchar_t) 0x2265)

#define UNICODE_HOUSE					     ((wchar_t) 0x2302)
#define UNICODE_REVERSED_NOT_SIGN			     ((wchar_t) 0x2310)
#define UNICODE_TOP_HALF_INTEGRAL			     ((wchar_t) 0x2320)
#define UNICODE_BOTTOM_HALF_INTEGRAL			     ((wchar_t) 0x2321)

#define UNICODE_BOX_DRAWINGS_LIGHT_HORIZONTAL		     ((wchar_t) 0x2500)
#define	UNICODE_BOX_DRAWINGS_HEAVY_HORIZONTAL		     ((wchar_t) 0x2501)
#define UNICODE_BOX_DRAWINGS_LIGHT_VERTICAL		     ((wchar_t) 0x2502)
#define UNICODE_BOX_DRAWINGS_LIGHT_DOWN_AND_RIGHT	     ((wchar_t) 0x250c)
#define UNICODE_BOX_DRAWINGS_DOWN_LIGHT_AND_RIGHT_HEAVY	     ((wchar_t) 0x250d)
#define UNICODE_BOX_DRAWINGS_DOWN_HEAVY_AND_RIGHT_LIGHT	     ((wchar_t) 0x250e)
#define UNICODE_BOX_DRAWINGS_HEAVY_DOWN_AND_RIGHT	     ((wchar_t) 0x250f)
#define UNICODE_BOX_DRAWINGS_LIGHT_DOWN_AND_LEFT	     ((wchar_t) 0x2510)
#define UNICODE_BOX_DRAWINGS_LIGHT_UP_AND_RIGHT		     ((wchar_t) 0x2514)
#define UNICODE_BOX_DRAWINGS_UP_LIGHT_AND_RIGHT_HEAVY	     ((wchar_t) 0x2515)
#define UNICODE_BOX_DRAWINGS_UP_HEAVY_AND_RIGHT_LIGHT	     ((wchar_t) 0x2516)
#define UNICODE_BOX_DRAWINGS_HEAVY_UP_AND_RIGHT		     ((wchar_t) 0x2517)
#define UNICODE_BOX_DRAWINGS_LIGHT_UP_AND_LEFT		     ((wchar_t) 0x2518)
#define UNICODE_BOX_DRAWINGS_LIGHT_VERTICAL_AND_RIGHT	     ((wchar_t) 0x251c)
#define UNICODE_BOX_DRAWINGS_VERTICAL_LIGHT_AND_RIGHT_HEAVY  ((wchar_t) 0x251d)
#define UNICODE_BOX_DRAWINGS_UP_HEAVY_AND_RIGHT_UP_LIGHT     ((wchar_t) 0x251e)
#define UNICODE_BOX_DRAWINGS_DOWN_HEAVY_AND_RIGHT_UP_LIGHT   ((wchar_t) 0x251f)
#define UNICODE_BOX_DRAWINGS_VERTICAL_HEAVY_AND_RIGHT_LIGHT  ((wchar_t) 0x2520)
#define UNICODE_BOX_DRAWINGS_DOWN_LIGHT_AND_RIGHT_UP_HEAVY   ((wchar_t) 0x2521)
#define UNICODE_BOX_DRAWINGS_UP_LIGHT_AND_RIGHT_DOWN_HEAVY   ((wchar_t) 0x2522)
#define UNICODE_BOX_DRAWINGS_HEAVY_VERTICAL_AND_RIGHT	     ((wchar_t) 0x2523)
#define UNICODE_BOX_DRAWINGS_LIGHT_VERTICAL_AND_LEFT	     ((wchar_t) 0x2524)
#define UNICODE_BOX_DRAWINGS_LIGHT_DOWN_AND_HORIZONTAL	     ((wchar_t) 0x252c)
#define UNICODE_BOX_DRAWINGS_LEFT_HEAVY_AND_RIGHT_DOWN_LIGHT ((wchar_t) 0x252d)
#define UNICODE_BOX_DRAWINGS_RIGHT_HEAVY_AND_LEFT_DOWN_LIGHT ((wchar_t) 0x252e)
#define UNICODE_BOX_DRAWINGS_DOWN_LIGHT_AND_HORIZONTAL_HEAVY ((wchar_t) 0x252f)
#define UNICODE_BOX_DRAWINGS_DOWN_HEAVY_AND_HORIZONTAL_LIGHT ((wchar_t) 0x2530)
#define UNICODE_BOX_DRAWINGS_RIGHT_LIGHT_AND_LEFT_DOWN_HEAVY ((wchar_t) 0x2531)
#define UNICODE_BOX_DRAWINGS_LEFT_LIGHT_AND_RIGHT_DOWN_HEAVY ((wchar_t) 0x2532)
#define UNICODE_BOX_DRAWINGS_HEAVY_DOWN_AND_HORIZONTAL	     ((wchar_t) 0x2533)
#define UNICODE_BOX_DRAWINGS_LIGHT_UP_AND_HORIZONTAL	     ((wchar_t) 0x2534)
#define UNICODE_BOX_DRAWINGS_LEFT_HEAVY_AND_RIGHT_UP_LIGHT   ((wchar_t) 0x2535)
#define UNICODE_BOX_DRAWINGS_RIGHT_HEAVY_AND_LEFT_UP_LIGHT   ((wchar_t) 0x2536)
#define UNICODE_BOX_DRAWINGS_UP_LIGHT_AND_HORIZONTAL_HEAVY   ((wchar_t) 0x2537)
#define UNICODE_BOX_DRAWINGS_UP_HEAVY_AND_HORIZONTAL_LIGHT   ((wchar_t) 0x2538)
#define UNICODE_BOX_DRAWINGS_RIGHT_LIGHT_AND_LEFT_UP_HEAVY   ((wchar_t) 0x2539)
#define UNICODE_BOX_DRAWINGS_LEFT_LIGHT_AND_RIGHT_UP_HEAVY   ((wchar_t) 0x253a)
#define UNICODE_BOX_DRAWINGS_HEAVY_UP_AND_HORIZONTAL	     ((wchar_t) 0x253b)
#define UNICODE_BOX_DRAWINGS_LIGHT_VERTICAL_AND_HORIZONTAL   ((wchar_t) 0x253c)
#define UNICODE_BOX_DRAWINGS_LEFT_HEAVY_AND_RIGHT_VERTICAL_LIGHT \
							     ((wchar_t) 0x253d)
#define UNICODE_BOX_DRAWINGS_RIGHT_HEAVY_AND_LEFT_VERTICAL_LIGHT \
							     ((wchar_t) 0x253e)
#define UNICODE_BOX_DRAWINGS_VERTICAL_LIGHT_AND_HORIZONTAL_HEAVY \
							     ((wchar_t) 0x253f)
#define UNICODE_BOX_DRAWINGS_UP_HEAVY_AND_DOWN_HORIZONTAL_LIGHT \
							     ((wchar_t) 0x2540)
#define UNICODE_BOX_DRAWINGS_DOWN_HEAVY_AND_UP_HORIZONTAL_LIGHT \
							     ((wchar_t) 0x2541)
#define UNICODE_BOX_DRAWINGS_VERTICAL_HEAVY_AND_HORIZONTAL_LIGHT \
							     ((wchar_t) 0x2542)
#define UNICODE_BOX_DRAWINGS_LEFT_UP_HEAVY_AND_RIGHT_DOWN_LIGHT \
							     ((wchar_t) 0x2543)
#define UNICODE_BOX_DRAWINGS_RIGHT_UP_HEAVY_AND_LEFT_DOWN_LIGHT \
							     ((wchar_t) 0x2544)
#define UNICODE_BOX_DRAWINGS_LEFT_DOWN_HEAVY_AND_RIGHT_UP_LIGHT \
							     ((wchar_t) 0x2545)
#define UNICODE_BOX_DRAWINGS_RIGHT_DOWN_HEAVY_AND_LEFT_UP_LIGHT \
							     ((wchar_t) 0x2546)
#define UNICODE_BOX_DRAWINGS_DOWN_LIGHT_AND_UP_HORIZONTAL_HEAVY \
							     ((wchar_t) 0x2547)
#define UNICODE_BOX_DRAWINGS_UP_LIGHT_AND_DOWN_HORIZONTAL_HEAVY \
							     ((wchar_t) 0x2548)
#define UNICODE_BOX_DRAWINGS_RIGHT_LIGHT_AND_LEFT_VERTICAL_HEAVY \
							     ((wchar_t) 0x2549)
#define UNICODE_BOX_DRAWINGS_LEFT_LIGHT_AND_RIGHT_VERTICAL_HEAVY \
							     ((wchar_t) 0x254a)
#define UNICODE_BOX_DRAWINGS_HEAVY_VERTICAL_AND_HORIZONTAL \
							     ((wchar_t) 0x254b)
#define UNICODE_BOX_DRAWINGS_DOUBLE_HORIZONTAL		     ((wchar_t) 0x2550)
#define UNICODE_BOX_DRAWINGS_DOUBLE_VERTICAL		     ((wchar_t) 0x2551)
#define UNICODE_BOX_DRAWINGS_DOWN_SINGLE_AND_RIGHT_DOUBLE    ((wchar_t) 0x2552)
#define UNICODE_BOX_DRAWINGS_DOWN_DOUBLE_AND_RIGHT_SINGLE    ((wchar_t) 0x2553)
#define UNICODE_BOX_DRAWINGS_DOUBLE_DOWN_AND_RIGHT	     ((wchar_t) 0x2554)
#define UNICODE_BOX_DRAWINGS_DOWN_SINGLE_AND_LEFT_DOUBLE     ((wchar_t) 0x2555)
#define UNICODE_BOX_DRAWINGS_DOWN_DOUBLE_AND_LEFT_SINGLE     ((wchar_t) 0x2556)
#define UNICODE_BOX_DRAWINGS_DOUBLE_DOWN_AND_LEFT	     ((wchar_t) 0x2557)
#define UNICODE_BOX_DRAWINGS_UP_SINGLE_AND_RIGHT_DOUBLE	     ((wchar_t) 0x2558)
#define UNICODE_BOX_DRAWINGS_UP_DOUBLE_AND_RIGHT_SINGLE	     ((wchar_t) 0x2559)
#define UNICODE_BOX_DRAWINGS_DOUBLE_UP_AND_RIGHT	     ((wchar_t) 0x255a)
#define UNICODE_BOX_DRAWINGS_UP_SINGLE_AND_LEFT_DOUBLE       ((wchar_t) 0x255b)
#define UNICODE_BOX_DRAWINGS_UP_DOUBLE_AND_LEFT_SINGLE       ((wchar_t) 0x255c)
#define UNICODE_BOX_DRAWINGS_DOUBLE_UP_AND_LEFT		     ((wchar_t) 0x255d)
#define UNICODE_BOX_DRAWINGS_VERTICAL_SINGLE_AND_RIGHT_DOUBLE \
							     ((wchar_t) 0x255e)
#define UNICODE_BOX_DRAWINGS_VERTICAL_DOUBLE_AND_RIGHT_SINGLE \
							     ((wchar_t) 0x255f)
#define UNICODE_BOX_DRAWINGS_DOUBLE_VERTICAL_AND_RIGHT	     ((wchar_t) 0x2560)
#define UNICODE_BOX_DRAWINGS_VERTICAL_SINGLE_AND_LEFT_DOUBLE ((wchar_t) 0x2561)
#define UNICODE_BOX_DRAWINGS_VERTICAL_DOUBLE_AND_LEFT_SINGLE ((wchar_t) 0x2562)
#define UNICODE_BOX_DRAWINGS_DOUBLE_VERTICAL_AND_LEFT	     ((wchar_t) 0x2563)
#define UNICODE_BOX_DRAWINGS_DOWN_SINGLE_AND_HORIZONTAL_DOUBLE \
							     ((wchar_t) 0x2564)
#define UNICODE_BOX_DRAWINGS_DOWN_DOUBLE_AND_HORIZONTAL_SINGLE \
							     ((wchar_t) 0x2565)
#define UNICODE_BOX_DRAWINGS_DOUBLE_DOWN_AND_HORIZONTAL      ((wchar_t) 0x2566)
#define UNICODE_BOX_DRAWINGS_UP_SINGLE_AND_HORIZONTAL_DOUBLE ((wchar_t) 0x2567)
#define UNICODE_BOX_DRAWINGS_UP_DOUBLE_AND_HORIZONTAL_SINGLE ((wchar_t) 0x2568)
#define UNICODE_BOX_DRAWINGS_DOUBLE_UP_AND_HORIZONTAL	     ((wchar_t) 0x2569)
#define UNICODE_BOX_DRAWINGS_VERTICAL_SINGLE_AND_HORIZONTAL_DOUBLE \
							     ((wchar_t) 0x256a)
#define UNICODE_BOX_DRAWINGS_VERTICAL_DOUBLE_AND_HORIZONTAL_SINGLE \
							     ((wchar_t) 0x256b)
#define UNICODE_BOX_DRAWINGS_DOUBLE_VERTICAL_AND_HORIZONTAL  ((wchar_t) 0x256c)
#define UNICODE_BOX_DRAWINGS_LIGHT_ARC_DOWN_AND_RIGHT	     ((wchar_t) 0x256d)
#define UNICODE_BOX_DRAWINGS_LIGHT_ARC_UP_AND_RIGHT	     ((wchar_t) 0x2570)
#define UNICODE_BOX_DRAWINGS_LIGHT_DIAGONAL_UPPER_RIGHT_TO_LOWER_LEFT \
							     ((wchar_t) 0x2571)
#define UNICODE_BOX_DRAWINGS_LIGHT_DIAGONAL_UPPER_LEFT_TO_LOWER_RIGHT \
							     ((wchar_t) 0x2572)
#define UNICODE_BOX_DRAWINGS_LIGHT_DIAGONAL_CROSS	     ((wchar_t) 0x2573)
#define UNICODE_BOX_DRAWINGS_LIGHT_RIGHT		     ((wchar_t) 0x2576)
#define UNICODE_BOX_DRAWINGS_HEAVY_RIGHT		     ((wchar_t) 0x257a)
#define UNICODE_BOX_DRAWINGS_LIGHT_LEFT_AND_HEAVY_RIGHT	     ((wchar_t) 0x257c)
#define UNICODE_BOX_DRAWINGS_HEAVY_LEFT_AND_LIGHT_RIGHT	     ((wchar_t) 0x257e)

#define UNICODE_UPPER_HALF_BLOCK			     ((wchar_t) 0x2580)
#define UNICODE_LOWER_ONE_EIGHTH_BLOCK			     ((wchar_t) 0x2581)
#define UNICODE_LOWER_ONE_QUARTER_BLOCK			     ((wchar_t) 0x2582)
#define UNICODE_LOWER_THREE_EIGHTHS_BLOCK		     ((wchar_t) 0x2583)
#define UNICODE_LOWER_HALF_BLOCK			     ((wchar_t) 0x2584)
#define UNICODE_LOWER_FIVE_EIGHTHS_BLOCK		     ((wchar_t) 0x2585)
#define UNICODE_LOWER_THREE_QUARTERS_BLOCK		     ((wchar_t) 0x2586)
#define UNICODE_LOWER_SEVEN_EIGHTHS_BLOCK		     ((wchar_t) 0x2587)
#define UNICODE_FULL_BLOCK				     ((wchar_t) 0x2588)
#define UNICODE_LEFT_HALF_BLOCK				     ((wchar_t) 0x258c)
#define UNICODE_RIGHT_HALF_BLOCK			     ((wchar_t) 0x2590)
#define UNICODE_LIGHT_SHADE				     ((wchar_t) 0x2591)
#define UNICODE_MEDIUM_SHADE				     ((wchar_t) 0x2592)
#define UNICODE_DARK_SHADE				     ((wchar_t) 0x2593)
#define UNICODE_UPPER_ONE_EIGHTH_BLOCK			     ((wchar_t) 0x2594)
#define UNICODE_RIGHT_ONE_EIGHTH_BLOCK			     ((wchar_t) 0x2595)
#define UNICODE_QUADRANT_LOWER_LEFT			     ((wchar_t) 0x2596)
#define UNICODE_QUADRANT_LOWER_RIGHT			     ((wchar_t) 0x2597)
#define UNICODE_QUADRANT_UPPER_LEFT			     ((wchar_t) 0x2598)
#define UNICODE_QUADRANT_UPPER_LEFT_AND_LOWER_LEFT_AND_LOWER_RIGHT \
							     ((wchar_t) 0x2599)
#define UNICODE_QUADRANT_UPPER_LEFT_AND_LOWER_RIGHT	     ((wchar_t) 0x259a)
#define UNICODE_QUADRANT_UPPER_LEFT_AND_UPPER_RIGHT_AND_LOWER_LEFT \
							     ((wchar_t) 0x259b)
#define UNICODE_QUADRANT_UPPER_LEFT_AND_UPPER_RIGHT_AND_LOWER_RIGHT \
							     ((wchar_t) 0x259c)
#define UNICODE_QUADRANT_UPPER_RIGHT			     ((wchar_t) 0x259d)
#define UNICODE_QUADRANT_UPPER_RIGHT_AND_LOWER_LEFT	     ((wchar_t) 0x259e)
#define UNICODE_QUADRANT_UPPER_RIGHT_AND_LOWER_LEFT_AND_LOWER_RIGHT \
							     ((wchar_t) 0x259f)

#define UNICODE_BLACK_SQUARE				     ((wchar_t) 0x25a0)
#define UNICODE_BLACK_RECTANGLE				     ((wchar_t) 0x25ac)
#define UNICODE_BLACK_UP_POINTING_TRIANGLE		     ((wchar_t) 0x25b2)
#define UNICODE_BLACK_RIGHT_POINTING_TRIANGLE		     ((wchar_t) 0x25b6)
#define UNICODE_BLACK_DOWN_POINTING_TRIANGLE		     ((wchar_t) 0x25bc)
#define UNICODE_BLACK_LEFT_POINTING_TRIANGLE		     ((wchar_t) 0x25c0)
#define UNICODE_WHITE_CIRCLE				     ((wchar_t) 0x25cb)
#define UNICODE_INVERSE_BULLET				     ((wchar_t) 0x25d8)
#define UNICODE_INVERSE_WHITE_CIRCLE			     ((wchar_t) 0x25d9)

#define UNICODE_WHITE_SMILING_FACE			     ((wchar_t) 0x263a)
#define UNICODE_BLACK_SMILING_FACE			     ((wchar_t) 0x263b)
#define UNICODE_WHITE_SUN_WITH_RAYS			     ((wchar_t) 0x263c)
#define UNICODE_FEMALE_SIGN				     ((wchar_t) 0x2640)
#define UNICODE_MALE_SIGN				     ((wchar_t) 0x2642)
#define UNICODE_BLACK_SPADE_SUIT			     ((wchar_t) 0x2660)
#define UNICODE_BLACK_CLUB_SUIT				     ((wchar_t) 0x2663)
#define UNICODE_BLACK_HEART_SUIT			     ((wchar_t) 0x2665)
#define UNICODE_BLACK_DIAMOND_SUIT			     ((wchar_t) 0x2666)
#define UNICODE_EIGHTH_NOTE				     ((wchar_t) 0x266a)
#define UNICODE_BEAMED_EIGHTH_NOTES			     ((wchar_t) 0x266b)

#define UNICODE_PRIVATE_USE_AREA			     ((wchar_t) 0xe000)
#define UNICODE_PRIVATE_USE_AREA_LAST			     ((wchar_t) 0xf8ff)

#define UNICODE_REPLACEMENT_CHARACTER			     ((wchar_t) 0xfffd)

#endif	/* _UNICODE_H_ */
