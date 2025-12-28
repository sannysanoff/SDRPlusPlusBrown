/* TETRA TAA1 suite implementation */
/*
 * Copyright (C) 2023 Midnight Blue B.V.
 *
 * Author: Wouter Bokslag <w.bokslag [ ] midnightblue [ ] nl>
 *
 * SPDX-License-Identifier: AGPL-3.0+
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * See the COPYING file in the main directory for details.
 */

#include <stdio.h>
#include <inttypes.h>
#include <assert.h>
#include <string.h>

#include "taa1.h"
#include "hurdle.h"

void transform_80_to_120(const uint8_t *lpBuffer, uint8_t *lpBufferOut)
{
	lpBufferOut[0] = lpBuffer[0] + lpBuffer[9];
	lpBufferOut[1] = lpBuffer[0];
	lpBufferOut[2] = lpBuffer[9];
	lpBufferOut[3] = lpBuffer[1] + lpBuffer[8];
	lpBufferOut[4] = lpBuffer[1];
	lpBufferOut[5] = lpBuffer[8];
	lpBufferOut[6] = lpBuffer[2] + lpBuffer[7];
	lpBufferOut[7] = lpBuffer[2];
	lpBufferOut[8] = lpBuffer[7];
	lpBufferOut[9] = lpBuffer[3] + lpBuffer[6];
	lpBufferOut[10] = lpBuffer[3];
	lpBufferOut[11] = lpBuffer[6];
	lpBufferOut[12] = lpBuffer[4] + lpBuffer[5];
	lpBufferOut[13] = lpBuffer[4];
	lpBufferOut[14] = lpBuffer[5];
}

void transform_80_to_128(const uint8_t *lpBuffer, uint8_t *lpBufferOut)
{
	transform_80_to_120(lpBuffer, lpBufferOut + 1);
	lpBufferOut[0] = lpBufferOut[1] ^ lpBufferOut[4] ^ lpBufferOut[7] ^ lpBufferOut[10] ^ lpBufferOut[13];
}

void transform_80_to_120_alt(const uint8_t *lpBuffer, uint8_t *lpBufferOut)
{
	lpBufferOut[0] = lpBuffer[0];
	lpBufferOut[1] = lpBuffer[1];
	lpBufferOut[2] = lpBufferOut[0] ^ lpBufferOut[1];
	lpBufferOut[3] = lpBuffer[2];
	lpBufferOut[4] = lpBuffer[3];
	lpBufferOut[5] = lpBufferOut[3] ^ lpBufferOut[4];
	lpBufferOut[6] = lpBuffer[4];
	lpBufferOut[7] = lpBuffer[5];
	lpBufferOut[8] = lpBufferOut[6] ^ lpBufferOut[7];
	lpBufferOut[9] = lpBuffer[6];
	lpBufferOut[10] = lpBuffer[7];
	lpBufferOut[11] = lpBufferOut[9] ^ lpBufferOut[10];
	lpBufferOut[12] = lpBuffer[8];
	lpBufferOut[13] = lpBuffer[9];
	lpBufferOut[14] = lpBufferOut[12] ^ lpBufferOut[13];
}

void transform_80_to_128_alt(const uint8_t *lpBuffer, uint8_t *lpBufferOut)
{
	transform_80_to_120_alt(lpBuffer, lpBufferOut);
	lpBufferOut[15] = lpBufferOut[2] + lpBufferOut[5] + lpBufferOut[8] + lpBufferOut[11] + lpBufferOut[14];
}

void transform_88_to_120(const uint8_t *lpBuffer, uint8_t *lpBufferOut)
{
	lpBufferOut[0] = lpBuffer[0];
	lpBufferOut[1] = lpBuffer[1];
	lpBufferOut[2] = lpBuffer[0] ^ lpBuffer[1];
	lpBufferOut[3] = lpBuffer[2];
	lpBufferOut[4] = lpBuffer[3];
	lpBufferOut[5] = lpBuffer[4];
	lpBufferOut[6] = lpBuffer[2] ^ lpBuffer[3] ^ lpBuffer[4];
	lpBufferOut[7] = lpBuffer[5];
	lpBufferOut[8] = lpBuffer[6];
	lpBufferOut[9] = lpBuffer[7];
	lpBufferOut[10] = lpBuffer[5] ^ lpBuffer[6] ^ lpBuffer[7];
	lpBufferOut[11] = lpBuffer[8];
	lpBufferOut[12] = lpBuffer[9];
	lpBufferOut[13] = lpBuffer[10];
	lpBufferOut[14] = lpBuffer[8] ^ lpBuffer[9] ^ lpBuffer[10];
}

void transform_120_to_88(const uint8_t *lpBuffer, uint8_t *lpBufferOut)
{
	lpBufferOut[0] = lpBuffer[0];
	lpBufferOut[1] = lpBuffer[1];
	lpBufferOut[2] = lpBuffer[3];
	lpBufferOut[3] = lpBuffer[4];
	lpBufferOut[4] = lpBuffer[5];
	lpBufferOut[5] = lpBuffer[7];
	lpBufferOut[6] = lpBuffer[8];
	lpBufferOut[7] = lpBuffer[9];
	lpBufferOut[8] = lpBuffer[11];
	lpBufferOut[9] = lpBuffer[12];
	lpBufferOut[10] = lpBuffer[13];
}

void transform_120_to_80_alt(const uint8_t *lpBuffer, uint8_t *lpBufferOut)
{
	lpBufferOut[0] = lpBuffer[0];
	lpBufferOut[1] = lpBuffer[1];
	lpBufferOut[2] = lpBuffer[3];
	lpBufferOut[3] = lpBuffer[4];
	lpBufferOut[4] = lpBuffer[6];
	lpBufferOut[5] = lpBuffer[7];
	lpBufferOut[6] = lpBuffer[9];
	lpBufferOut[7] = lpBuffer[10];
	lpBufferOut[8] = lpBuffer[12];
	lpBufferOut[9] = lpBuffer[13];
}

void ta11_ta41(uint8_t *lpKeyK, uint8_t *lpChallengeRs, uint8_t *lpKsOut)
{
	uint8_t abChallengeExpanded[16];
	transform_80_to_128_alt(lpChallengeRs, abChallengeExpanded);
	HURDLE_enc_cbc(lpKsOut, abChallengeExpanded, lpKeyK);
}

void ta12_ta22(uint8_t *lpKeyKs, uint8_t *lpRand, uint8_t *lpResOut, uint8_t *lpDckOut)
{
	uint8_t abRandExpanded[16];
	uint8_t abCiphertext[16];
	transform_80_to_128_alt(lpRand, abRandExpanded);
	HURDLE_enc_cbc(abCiphertext, abRandExpanded, lpKeyKs);

	lpResOut[0] = abCiphertext[0] ^ abCiphertext[3];
	lpResOut[1] = abCiphertext[6];
	lpResOut[2] = abCiphertext[9];
	lpResOut[3] = abCiphertext[12] ^ abCiphertext[15];

	lpDckOut[0] = abCiphertext[1];
	lpDckOut[1] = abCiphertext[2];
	lpDckOut[2] = abCiphertext[4];
	lpDckOut[3] = abCiphertext[5];
	lpDckOut[4] = abCiphertext[7];
	lpDckOut[5] = abCiphertext[8];
	lpDckOut[6] = abCiphertext[10];
	lpDckOut[7] = abCiphertext[11];
	lpDckOut[8] = abCiphertext[13];
	lpDckOut[9] = abCiphertext[14];
}

void ta21(uint8_t *lpKeyK, uint8_t *lpChallengeRs, uint8_t *lpKspOut)
{
	uint8_t abChallengeExpanded[16];
	uint8_t abChallengeReversed[10];
	int i;

	for (i = 0; i < 10; i++)
		abChallengeReversed[i] = lpChallengeRs[9-i];

	transform_80_to_128_alt(abChallengeReversed, abChallengeExpanded);
	HURDLE_enc_cbc(lpKspOut, abChallengeExpanded, lpKeyK);
}

void ta31(uint8_t *lpUnsealedCck, uint8_t *lpCckId, uint8_t *lpDck, uint8_t *lpSealedCckOut)
{
	uint8_t abUnsealedPadded[16];
	uint8_t abHurdleKey[16];
	uint8_t abAdjustedDck[10];
	uint8_t abSealed[16];
	int i;

	transform_80_to_120_alt(lpUnsealedCck, abUnsealedPadded);
	abUnsealedPadded[15] = '\0';

	for (i = 0; i < 10; i++)
		abAdjustedDck[i] = lpDck[i] ^ lpCckId[i & 1];

	transform_80_to_128(abAdjustedDck, abHurdleKey);
	HURDLE_enc_cbc(abSealed, abUnsealedPadded, abHurdleKey);
	/* ciphertext stealing */
	memcpy(lpSealedCckOut, abSealed, 7);
	memcpy(lpSealedCckOut + 7, abSealed + 8, 8);
}

void ta32(uint8_t *lpSealedCck, uint8_t *lpCckId, uint8_t *lpDck, uint8_t *lpUnsealedCckOut, uint8_t *lpMfOut)
{
	uint8_t abHurdleKey[16];
	uint8_t abAdjustedDck[10];
	uint8_t abUnsealedPadded[16];
	int i;

	for (i = 0; i < 10; i++)
		abAdjustedDck[i] = lpDck[i] ^ lpCckId[i & 1];

	transform_80_to_128(abAdjustedDck, abHurdleKey);

	HURDLE_dec_cts(abUnsealedPadded, lpSealedCck, abHurdleKey);
	transform_120_to_80_alt(abUnsealedPadded, lpUnsealedCckOut);

	*lpMfOut =
		((abUnsealedPadded[0] ^ abUnsealedPadded[1]) != abUnsealedPadded[2]) ||
		((abUnsealedPadded[3] ^ abUnsealedPadded[4]) != abUnsealedPadded[5]) ||
		((abUnsealedPadded[6] ^ abUnsealedPadded[7]) != abUnsealedPadded[8]) ||
		((abUnsealedPadded[9] ^ abUnsealedPadded[10]) != abUnsealedPadded[11]) ||
		((abUnsealedPadded[12] ^ abUnsealedPadded[13]) != abUnsealedPadded[14]);
}

void ta51(uint8_t *lpUnsealed, uint8_t *lpVn, uint8_t *lpKey, uint8_t *lpKeyN, uint8_t *lpSealedOut)
{
	uint8_t abUnsealed[11];
	uint8_t abUnsealedPadded[16];
	uint8_t abSealed[16];
	uint8_t abAdjustedKey[16];
	int i;

	assert((*lpKeyN & 0xe0) == 0);

	memcpy(abUnsealed, lpUnsealed, 10);
	abUnsealed[10] = *lpKeyN;
	transform_88_to_120(abUnsealed, abUnsealedPadded);
	abUnsealedPadded[15] = '\0';

	for (i = 0; i < 16; i++)
		abAdjustedKey[i] = lpKey[i] ^ lpVn[i & 1];

	HURDLE_enc_cbc(abSealed, abUnsealedPadded, abAdjustedKey);
	/* ciphertext stealing */
	memcpy(lpSealedOut, abSealed, 7);
	memcpy(lpSealedOut + 7, abSealed + 8, 8);
}

void ta52(uint8_t *lpSealed, uint8_t *lpKey, uint8_t *lpVn, uint8_t *lpUnsealedOut, uint8_t *lpMfOut, uint8_t *lpKeyNOut)
{
	uint8_t abAdjustedKey[16];
	uint8_t abUnsealedPadded[15];
	uint8_t abUnsealed[11];
	int i;

	for (i = 0; i < 16; i++)
		abAdjustedKey[i] = lpKey[i] ^ lpVn[i & 1];

	HURDLE_dec_cts(abUnsealedPadded, lpSealed, abAdjustedKey);
	transform_120_to_88(abUnsealedPadded, abUnsealed);
	memcpy(lpUnsealedOut, abUnsealed, 10);
	*lpKeyNOut = abUnsealed[10];
	*lpMfOut =
		((abUnsealedPadded[0] ^ abUnsealedPadded[1]) != abUnsealedPadded[2]) ||
		((abUnsealedPadded[3] ^ abUnsealedPadded[4] ^ abUnsealedPadded[5]) != abUnsealedPadded[6]) ||
		((abUnsealedPadded[7] ^ abUnsealedPadded[8] ^ abUnsealedPadded[9]) != abUnsealedPadded[10]) ||
		((abUnsealedPadded[11] ^ abUnsealedPadded[12] ^ abUnsealedPadded[13]) != abUnsealedPadded[14]) ||
		abUnsealed[10] & 0xe0;
}

void ta71(uint8_t *lpGck, uint8_t *lpCck, uint8_t *lpMgckOut)
{
	uint8_t abCiphertext[16];
	uint8_t abHurdleKey[16];
	uint8_t abPlaintextExpanded[16];
	uint8_t abPlaintext[10];
	int i;

	for (i = 0; i < 10; i++)
		abPlaintext[i] = lpGck[i] ^ lpCck[i];

	transform_80_to_128_alt(abPlaintext, abPlaintextExpanded);

	abHurdleKey[0] = lpGck[0];
	abHurdleKey[1] = lpGck[1];
	abHurdleKey[2] = lpGck[2];
	abHurdleKey[3] = lpGck[3];
	abHurdleKey[4] = lpGck[4];
	abHurdleKey[5] = lpGck[5];
	abHurdleKey[6] = lpGck[6] ^ lpCck[0];
	abHurdleKey[7] = lpGck[7] ^ lpCck[1];
	abHurdleKey[8] = lpGck[8] ^ lpCck[2];
	abHurdleKey[9] = lpGck[9] ^ lpCck[3];
	abHurdleKey[10] = lpCck[4];
	abHurdleKey[11] = lpCck[5];
	abHurdleKey[12] = lpCck[6];
	abHurdleKey[13] = lpCck[7];
	abHurdleKey[14] = lpCck[8];
	abHurdleKey[15] = lpCck[9];

	HURDLE_enc_cbc(abCiphertext, abPlaintextExpanded, abHurdleKey);

	memcpy(lpMgckOut, &abCiphertext[3], 10);
}

void ta81(uint8_t *lpUnsealedGck, uint8_t *lpGckVn, uint8_t *lpGckN, uint8_t *lpKey, uint8_t *lpSealedGckOut)
{
	uint8_t abUnsealedPadded[16];
	uint8_t abSealed[16];
	uint8_t abAdjustedKey[16];
	int i;

	abUnsealedPadded[0] = lpUnsealedGck[0];
	abUnsealedPadded[1] = lpUnsealedGck[1];
	abUnsealedPadded[2] = lpUnsealedGck[2];
	abUnsealedPadded[3] = lpUnsealedGck[3];
	abUnsealedPadded[4] = abUnsealedPadded[0] ^ abUnsealedPadded[1] ^ abUnsealedPadded[2] ^ abUnsealedPadded[3];
	abUnsealedPadded[5] = lpUnsealedGck[4];
	abUnsealedPadded[6] = lpUnsealedGck[5];
	abUnsealedPadded[7] = lpUnsealedGck[6];
	abUnsealedPadded[8] = lpUnsealedGck[7];
	abUnsealedPadded[9] = abUnsealedPadded[5] ^ abUnsealedPadded[6] ^ abUnsealedPadded[7] ^ abUnsealedPadded[8];
	abUnsealedPadded[10] = lpUnsealedGck[8];
	abUnsealedPadded[11] = lpUnsealedGck[9];
	abUnsealedPadded[12] = lpGckN[0];
	abUnsealedPadded[13] = lpGckN[1];
	abUnsealedPadded[14] = abUnsealedPadded[10] ^ abUnsealedPadded[11] ^ abUnsealedPadded[12] ^ abUnsealedPadded[13];
	abUnsealedPadded[15] = '\0';

	for (i = 0; i < 16; i++)
		abAdjustedKey[i] = lpKey[i] ^ lpGckVn[i & 1];

	HURDLE_enc_cbc(abSealed, abUnsealedPadded, abAdjustedKey);
	/* ciphertext stealing */
	memcpy(lpSealedGckOut, abSealed, 7);
	memcpy(lpSealedGckOut + 7, abSealed + 8, 8);
}

void ta82(uint8_t *lpSealedGck, uint8_t *lpGckVn, uint8_t *lpKey, uint8_t *lpUnsealedGckOut, uint8_t *lpMfOut, uint8_t *lpGckNOut)
{
	uint8_t abAdjustedKey[16];
	uint8_t abUnsealedPadded[15];
	int i;

	for (i = 0; i < 16; i++)
		abAdjustedKey[i] = lpKey[i] ^ lpGckVn[i & 1];

	HURDLE_dec_cts(abUnsealedPadded, lpSealedGck, abAdjustedKey);

	lpUnsealedGckOut[0] = abUnsealedPadded[0];
	lpUnsealedGckOut[1] = abUnsealedPadded[1];
	lpUnsealedGckOut[2] = abUnsealedPadded[2];
	lpUnsealedGckOut[3] = abUnsealedPadded[3];
	lpUnsealedGckOut[4] = abUnsealedPadded[5];
	lpUnsealedGckOut[5] = abUnsealedPadded[6];
	lpUnsealedGckOut[6] = abUnsealedPadded[7];
	lpUnsealedGckOut[7] = abUnsealedPadded[8];
	lpUnsealedGckOut[8] = abUnsealedPadded[10];
	lpUnsealedGckOut[9] = abUnsealedPadded[11];

	lpGckNOut[0] = abUnsealedPadded[12];
	lpGckNOut[1] = abUnsealedPadded[13];

	*lpMfOut =
		(abUnsealedPadded[14] != (abUnsealedPadded[10] ^ abUnsealedPadded[11] ^ abUnsealedPadded[12] ^ abUnsealedPadded[13])) ||
		(abUnsealedPadded[9] != (abUnsealedPadded[5] ^ abUnsealedPadded[6] ^ abUnsealedPadded[7] ^ abUnsealedPadded[8])) ||
		(abUnsealedPadded[4] != (abUnsealedPadded[0] ^ abUnsealedPadded[1] ^ abUnsealedPadded[2] ^ abUnsealedPadded[3]));
}

void ta91(uint8_t *lpUnsealedGsko, uint8_t *lpGskoVn, uint8_t *lpKey, uint8_t *lpSealedGskoOut)
{
	ta81(lpUnsealedGsko, lpGskoVn, lpUnsealedGsko + 10, lpKey, lpSealedGskoOut);
}

void ta92(uint8_t *lpSealedGsko, uint8_t *lpGskoVn, uint8_t *lpKey, uint8_t *lpUnsealedGskoOut, uint8_t *lpMfOut)
{
	ta82(lpSealedGsko, lpGskoVn, lpKey, lpUnsealedGskoOut, lpMfOut, lpUnsealedGskoOut + 10);
}


void tb4(uint8_t *lpDck1, uint8_t *lpDck2, uint8_t *lpDckOut)
{
	int i;
	for (i = 0; i < 10; i++)
		lpDckOut[i] = lpDck1[i] ^ lpDck2[i];
}

void tb5(uint8_t *lpCn, uint8_t *lpLa, uint8_t *lpCc, uint8_t *lpCk, uint8_t *lpEckOut)
{
	uint32_t adwComputedEck[3];
	uint32_t adwInputCk[3];

	adwInputCk[0] = be16(*(uint16_t *)&lpCk[0]);
	adwInputCk[1] = be32(*(uint32_t *)&lpCk[2]);
	adwInputCk[2] = be32(*(uint32_t *)&lpCk[6]);

	uint16_t wCn = be16(*(uint16_t *)lpCn);
	uint16_t wLa = be16(*(uint16_t *)lpLa);
	uint8_t bCc = *lpCc;

	/*
	wCn = 12 bits = cn carrier number
	wLa = 14 bits = la location area code
	bCc =  6 bits = cc color code

	Computes:
	[ la:14 cn:12 cc:6 cn:12 cc:6 cn:12 cc:6 cn:12 ]
				xor
	[			ck:80                  ]
	*/

	assert((wCn & ~0xFFF) == 0);
	assert((wLa & ~0x3FFF) == 0);
	assert((bCc & ~0x3F) == 0);

	uint32_t dwMask0, dwMask1, dwMask2;
	dwMask0 = (wLa << 2) | (wCn >> 10);
	dwMask1 = (wCn << 22) | (bCc << 16) | (wCn << 4) | (bCc >> 2);
	dwMask2 = (bCc << 30) | (wCn << 18) | (bCc << 12) | wCn;

	adwComputedEck[0] = adwInputCk[0] ^ dwMask0;
	adwComputedEck[1] = adwInputCk[1] ^ dwMask1;
	adwComputedEck[2] = adwInputCk[2] ^ dwMask2;

	*(uint16_t *)(&lpEckOut[0]) = be16(adwComputedEck[0]);
	*(uint32_t *)(&lpEckOut[2]) = be32(adwComputedEck[1]);
	*(uint32_t *)(&lpEckOut[6]) = be32(adwComputedEck[2]);
}

void tb6(uint8_t *lpSck, uint8_t *lpCn, uint8_t *lpSsi, uint8_t *lpEckOut)
{
	uint32_t adwComputedEck[3];
	uint32_t adwInputSck[3];

	adwInputSck[0] = be16(*(uint16_t *)&lpSck[0]);
	adwInputSck[1] = be32(*(uint32_t *)&lpSck[2]);
	adwInputSck[2] = be32(*(uint32_t *)&lpSck[6]);

	uint16_t wCn = be16(*(uint16_t *)lpCn);
	uint32_t dwSsi = (be16(*(uint16_t *)lpSsi) << 8) | lpSsi[2];

	uint32_t dwMask0, dwMask1, dwMask2;
	/*
	Computes:
	[cn:12 ssi:24 cn:12 ssi:24 lsb(ssi):8 ]
			xor
	[		sck:80                ]
	*/

	dwMask0 = (wCn << 4) | (dwSsi >> 20);
	dwMask1 = (dwSsi << 12) | wCn;
	dwMask2 = (dwSsi << 8) | (dwSsi & 0xff);

	adwComputedEck[0] = adwInputSck[0] ^ dwMask0;
	adwComputedEck[1] = adwInputSck[1] ^ dwMask1;
	adwComputedEck[2] = adwInputSck[2] ^ dwMask2;

	*(uint16_t *)(&lpEckOut[0]) = be16(adwComputedEck[0]);
	*(uint32_t *)(&lpEckOut[2]) = be32(adwComputedEck[1]);
	*(uint32_t *)(&lpEckOut[6]) = be32(adwComputedEck[2]);
}

void tb7(uint8_t *lpGsko, uint8_t *lpEgskoOut)
{
	lpEgskoOut[0] = lpGsko[0];
	lpEgskoOut[1] = lpGsko[1];
	lpEgskoOut[2] = lpGsko[2];
	lpEgskoOut[3] = lpGsko[0] ^ lpGsko[1] ^ lpGsko[2];
	lpEgskoOut[4] = lpGsko[3];
	lpEgskoOut[5] = lpGsko[4];
	lpEgskoOut[6] = lpGsko[5];
	lpEgskoOut[7] = lpGsko[3] ^ lpGsko[4] ^ lpGsko[5];
	lpEgskoOut[8] = lpGsko[6];
	lpEgskoOut[9] = lpGsko[7];
	lpEgskoOut[10] = lpGsko[8];
	lpEgskoOut[11] = lpGsko[6] ^ lpGsko[7] ^ lpGsko[8];
	lpEgskoOut[12] = lpGsko[9];
	lpEgskoOut[13] = lpGsko[10];
	lpEgskoOut[14] = lpGsko[11];
	lpEgskoOut[15] = lpGsko[9] ^ lpGsko[10] ^ lpGsko[11];
}
