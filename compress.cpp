// ####################################
// #                                  #
// #       Ping of Death - Bot        #
// #                by                #
// #    Markus Klinge aka Count Floyd #
// #                                  #
// ####################################
//
// Started from the HPB-Bot Alpha Source
// by Botman so Credits for a lot of the basic
// HL Server/Client Stuff goes to him
//
// compress.cpp
//
// POD Data Compress Routines based on LZSS
//
// Actually I ripped this out of the AceBot Source and
// modified it to suit my needs.
// This is what the original (?) Author writes:
//
// Original file is Copyright(c), Steve Yeager 1998
//
// Not sure where I got this code, but thanks go to the
// author. I just rewote it to allow the use of buffers
// instead of files.
//
///////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <malloc.h>
#if defined __linux__
#include "bot_globals.h"
#endif

#define N 4096 // size of ring buffer
#define F 18 // upper limit for match_length
#define THRESHOLD 2 // encode string into position and length if match_length is
					// greater than this index for root of binary search trees
#define NIL N

unsigned long int
textsize = 0, // text size counter
codesize = 0, // code size counter
printcount = 0; // counter for reporting progress every 1K bytes
unsigned char
text_buf[N + F - 1]; // ring buffer of size N, with extra F-1 bytes to facilitate string
					 // comparison of longest match. Set by the InsertNode() procedure.
int   match_position, match_length,
lson[N + 1], rson[N + 257], dad[N + 1]; // left & right children & parents -- These
										// constitute binary search trees.

void InitTree(void) // initialize trees
{
	int i;

	// For i = 0 to N - 1, rson[i] and lson[i] will be the right and
	// left children of node i.  These nodes need not be initialized.
	// Also, dad[i] is the parent of node i.  These are initialized to
	// NIL  (= N), which stands for 'not used.'
	// For i = 0 to 255, rson[N + i + 1] is the root of the tree
	// for strings that begin with character i.  These are initialized
	// to NIL.  Note there are 256 trees.

	for (i = N + 1; i <= N + 256; i++)
		rson[i] = NIL;
	for (i = 0; i < N; i++)
		dad[i] = NIL;
}

void InsertNode(int r)
{
	// Inserts string of length F, text_buf[r..r+F-1], into one of the
	// trees (text_buf[r]'th tree) and returns the longest-match position
	// and length via the global variables match_position and match_length.
	// If match_length = F, then removes the old node in favor of the new
	// one, because the old one will be deleted sooner.
	// Note r plays double role, as tree node and position in buffer.

	int i, p, cmp;
	unsigned char* key;

	cmp = 1;
	key = &text_buf[r];
	p = N + 1 + key[0];
	rson[r] = lson[r] = NIL;
	match_length = 0;

	for (; ;)
	{
		if (cmp >= 0)
		{
			if (rson[p] != NIL)
				p = rson[p];
			else
			{
				rson[p] = r;
				dad[r] = p;
				return;
			}
		}
		else
		{
			if (lson[p] != NIL)
				p = lson[p];
			else
			{
				lson[p] = r;
				dad[r] = p;
				return;
			}
		}

		for (i = 1; i < F; i++)
			if ((cmp = key[i] - text_buf[p + i]) != 0)
				break;

		if (i > match_length)
		{
			match_position = p;
			if ((match_length = i) >= F)
				break;
		}
	}

	dad[r] = dad[p];
	lson[r] = lson[p];
	rson[r] = rson[p];
	dad[lson[p]] = r;
	dad[rson[p]] = r;

	if (rson[dad[p]] == p)
		rson[dad[p]] = r;
	else
		lson[dad[p]] = r;

	dad[p] = NIL; // remove p
}

void DeleteNode(int p) // deletes node p from tree
{
	int q;

	if (dad[p] == NIL)
		return; // not in tree

	if (rson[p] == NIL)
		q = lson[p];

	else if (lson[p] == NIL)
		q = rson[p];

	else
	{
		q = lson[p];

		if (rson[q] != NIL)
		{
			do
				q = rson[q];
			while (rson[q] != NIL);

			rson[dad[q]] = lson[q];
			dad[lson[q]] = dad[q];
			lson[q] = lson[p];
			dad[lson[p]] = q;
		}

		rson[q] = rson[p];
		dad[rson[p]] = q;
	}

	dad[q] = dad[p];

	if (rson[dad[p]] == p)
		rson[dad[p]] = q;
	else
		lson[dad[p]] = q;

	dad[p] = NIL;
}

int Encode(char* filename, unsigned char* header, int headersize, unsigned char* buffer, int bufsize)
{
	int i, len, r, s, last_match_length, code_buf_ptr;
	unsigned char code_buf[17], mask, c;
	int bufptr = 0;
	FILE* pOut;

#ifdef _WIN32
	fopen_s(&pOut, filename, "wb");
#else
	pOut = fopen(filename, "wb");
#endif
	if (pOut == NULL)
		return -1; // bail

	 // Write Header first
	fwrite(header, headersize, 1, pOut);

	InitTree(); // initialize trees

	// code_buf[1..16] saves eight units of code, and code_buf[0] works as eight flags,
	// "1" representing that the unit is an unencoded letter (1 byte), "0" a
	// position-and-length pair (2 bytes). Thus, eight units require at most 16 bytes of code.
	code_buf[0] = 0;
	code_buf_ptr = mask = 1;
	s = 0;
	r = N - F;

	for (i = s; i < r; i++)
		text_buf[i] = ' '; // Clear the buffer with any character that will appear often.

	for (len = 0; len < F && bufptr < bufsize; len++)
	{
		c = buffer[bufptr++];
		text_buf[r + len] = c; // Read F bytes into the last F bytes of the buffer
	}

	if ((textsize = len) == 0)
		return -1; // text of size zero

	 // Insert the F strings, each of which begins with one or more 'space' characters.
	 // Note the order in which these strings are inserted. This way, degenerate trees
	 // will be less likely to occur.
	for (i = 1; i <= F; i++)
		InsertNode(r - i);

	// Finally, insert the whole string just read. The global variables
	// match_length and match_position are set.
	InsertNode(r);

	do
	{
		if (match_length > len)
			match_length = len; // match_length may be spuriously long near the end of text.

		if (match_length <= THRESHOLD)
		{
			match_length = 1; // Not long enough match.  Send one byte.
			code_buf[0] |= mask; // 'send one byte' flag
			code_buf[code_buf_ptr++] = text_buf[r]; // Send uncoded.
		}
		else
		{
			// Send position and length pair. Note match_length > THRESHOLD.
			code_buf[code_buf_ptr++] = (unsigned char)match_position;
			code_buf[code_buf_ptr++] = (unsigned char)(match_position >> 4 & 0xf0 | match_length - (THRESHOLD + 1));
		}

		if ((mask <<= 1) == 0)
		{
			// Shift mask left one bit.
			for (i = 0; i < code_buf_ptr; i++)
				putc(code_buf[i], pOut); // Send at most 8 units of code together

			codesize += code_buf_ptr;
			code_buf[0] = 0;
			code_buf_ptr = mask = 1;
		}

		last_match_length = match_length;

		for (i = 0; i < last_match_length && bufptr < bufsize; i++)
		{
			c = buffer[bufptr++];
			DeleteNode(s);     // Delete old strings and
			text_buf[s] = c;  // read new bytes

			// If the position is near the end of buffer, extend the buffer to make
			// string comparison easier.
			if (s < F - 1)
				text_buf[s + N] = c;

			// Since this is a ring buffer, increment the position modulo N.
			s = s + 1 & N - 1;
			r = r + 1 & N - 1;
			InsertNode(r); // Register the string in text_buf[r..r+F-1]
		}

		while (i++ < last_match_length)
		{
			// After the end of text, no need to read, but buffer may not be empty.
			DeleteNode(s);

			s = s + 1 & N - 1;
			r = r + 1 & N - 1;

			if (--len)
				InsertNode(r);
		}
	} while (len > 0);  // until length of string to be processed is zero

	if (code_buf_ptr > 1)
	{
		// Send remaining code.
		for (i = 0; i < code_buf_ptr; i++)
			putc(code_buf[i], pOut);
		codesize += code_buf_ptr;
	}

	fclose(pOut);

	return codesize;
}

int Decode(char* filename, int headersize, unsigned char* buffer, int bufsize)
{
	// Be careful with your buffersize, will return an exit of -1 if failure

	int i, j, k, r, c;
	unsigned int flags;
	int bufptr = 0;
	FILE* pIn;

#ifdef _WIN32
	fopen_s(&pIn, filename, "rb");
#else
	pIn = fopen(filename, "rb");
#endif
	if (pIn == NULL)
		return -1; // bail

	 // Skip Header
	fseek(pIn, headersize, SEEK_SET);

	r = N - F;
	for (i = 0; i < r; i++)
		text_buf[i] = ' ';

	flags = 0;

	for (; ;)
	{
		if (((flags >>= 1) & 256) == 0)
		{
			if ((c = getc(pIn)) == EOF)
				break;
			flags = c | 0xff00; // uses higher byte cleverly
		}

		// to count eight
		if (flags & 1)
		{
			if ((c = getc(pIn)) == EOF)
				break;
			buffer[bufptr++] = (unsigned char)c;

			if (bufptr > bufsize)
				return -1; // check for overflow

			text_buf[r++] = (unsigned char)c;
			r &= N - 1;
		}
		else
		{
			if ((i = getc(pIn)) == EOF)
				break;
			if ((j = getc(pIn)) == EOF)
				break;

			i |= (j & 0xf0) << 4;
			j = (j & 0x0f) + THRESHOLD;

			for (k = 0; k <= j; k++)
			{
				c = text_buf[i + k & N - 1];
				buffer[bufptr++] = (unsigned char)c;

				if (bufptr > bufsize)
					return -1; // check for overflow

				text_buf[r++] = (unsigned char)c;
				r &= N - 1;
			}
		}
	}

	fclose(pIn);

	return bufptr; // return uncompressed size
}