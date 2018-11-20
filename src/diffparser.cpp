/*
	neon-diff - Application to colorify, highlight and beautify unified diffs.

	Copyright (C) 2018 - Mladen Milinkovic <maxrd2@smoothware.net>

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.

*/

#include "diffparser.h"

#include "neonapp.h"
#include "colors.h"

#include <stdlib.h>
#include <algorithm>
#include <cassert>


#define BUFFER_SIZE_INIT 8192
// when buffer becomes too small its size is doubled, unless bigger than this
#define BUFFER_MAX_SIZE_INC 8192 * 1024

Match::Match()
	: rem_(nullptr),
	  remEnd_(nullptr),
	  add_(nullptr),
	  addEnd_(nullptr),
	  len_(0)
{
}

Match::Match(const char *rem, const char *remEnd, const char *add, const char *addEnd, int len)
	: rem_(rem),
	  remEnd_(remEnd),
	  add_(add),
	  addEnd_(addEnd),
	  len_(len)
{
}


const DiffParser::LineHandler DiffParser::lineHandler_[LINE_HANDLER_SIZE] = {
	// diff header lines
	{"---", &DiffParser::handleFileInfoLine, false},
	{"+++", &DiffParser::handleFileInfoLine, false},
	{"@@", &DiffParser::handleRangeInfoLine, false},
	// block/range diff lines
	{" ", &DiffParser::handleContextLine, false},
	{"-", &DiffParser::handleRemLine, true},
	{"+", &DiffParser::handleAddLine, true}
};

DiffParser::DiffParser(FILE *inputStream)
	: input_(inputStream),
	  buf_(nullptr),
	  bufLen_(0),
	  bufSize_(BUFFER_SIZE_INIT),
	  line_(nullptr),
	  lineLen_(0),
	  blockRem_(nullptr),
	  blockAdd_(nullptr)
{
	buf_ = static_cast<char *>(malloc(bufSize_));
}

DiffParser::~DiffParser()
{
	if(buf_) {
		free(buf_);
		bufSize_ = bufLen_ = 0;
	}
	line_ = nullptr;
	lineLen_ = 0;
	blockRem_ = nullptr;
	blockAdd_ = nullptr;
}

bool
DiffParser::handlerForLine(const char *line, const char *id, int n)
{
	while(n && *line && *id) {
		if(*line == '\33') { // skip ansi chars
			while(n-- && *line++ != 'm');
			continue;
		}
		n--;
		if(*line++ != *id++)
			return false;
	}

	return *id == 0;
}

void
DiffParser::processInput()
{
	inBlock_ = false;

	while(readLine()) {
		// find a line handler
		int i = 0;
		while(i < LINE_HANDLER_SIZE && !handlerForLine(line_, lineHandler_[i].identifier, lineLen_))
			i++;

		const bool blockLine = i < LINE_HANDLER_SIZE && lineHandler_[i].blockLine;
		if(inBlock_ && !blockLine)
			processBlock();
		inBlock_ = blockLine;

		if(i < LINE_HANDLER_SIZE)
			(this->*lineHandler_[i].callback)();
		else
			handleGenericLine();

		if(!inBlock_)
			resetBuffer();
	}

	if(inBlock_)
		processBlock();
}

void
DiffParser::resetBuffer()
{
	bufLen_ = 0;
	line_ = nullptr;
	lineLen_ = 0;
	blockRem_ = nullptr;
	blockAdd_ = nullptr;
}

void
DiffParser::resizeBuffer()
{
	char *buf = buf_;
	bufSize_ += bufSize_ > BUFFER_MAX_SIZE_INC ? BUFFER_MAX_SIZE_INC : bufSize_;
	buf_ = static_cast<char *>(realloc(buf_, bufSize_));

	// change pointers to new buf address
	line_ = buf_ + (line_ - buf);
	if(blockRem_)
		blockRem_ = buf_ + (blockRem_ - buf);
	if(blockAdd_)
		blockAdd_ = buf_ + (blockAdd_ - buf);
}

bool
DiffParser::readLine()
{
	line_ = &buf_[bufLen_];
	lineLen_ = 0;

	while(!feof(input_)) {
		if(bufLen_ >= bufSize_)
			resizeBuffer();

		int ch = fgetc(input_);
		buf_[bufLen_] = ch;

		bufLen_++;
		lineLen_++;

		if(ch == '\n')
			break;
	}

	return !feof(input_);
}

void
DiffParser::stripLineAnsi(int stripIndent/* = 0 */)
{
	char *left = line_;
	char *right = line_;

	for(int i = 0; i < lineLen_;) {
		if(*right == '\33') { // skip ansi chars
			do {
				lineLen_--;
				bufLen_--;
			} while(i < lineLen_ && *right++ != 'm');
			continue;
		}
		if(stripIndent) {
			lineLen_--;
			bufLen_--;
			stripIndent--;
			right++;
		} else {
			i++;
			*left++ = *right++;
		}
	}
}

Match
DiffParser::longestMatch(const char *rem, const char *remEnd, const char *add, const char *addEnd)
{
	assert(rem <= remEnd);
	assert(add <= addEnd);

	Match best;

	const char *bSave = add;

	auto spaceCount = [](const char *buf, const char *bufEnd) -> int {
		int c = 0;
		while(buf + c < bufEnd && (buf[c] == ' ' || buf[c] == '\t' || buf[c] == '\n'))
			c++;
		return c;
	};

	while(remEnd - rem > best.len_) {
		const int iOffset = app->ignoreSpaces() ? spaceCount(rem, remEnd) : 0;
		while(addEnd - add > best.len_) {
			int i = iOffset;
			int j = 0;
			if(app->ignoreSpaces())
				j += spaceCount(add, addEnd);
			int len = 0;

			while(rem + i < remEnd && add + j < addEnd && rem[i] == add[j]) {
				len++;
				i++;
				j++;

				if(app->ignoreSpaces()) {
					const int si = spaceCount(rem + i, remEnd);
					const int sj = spaceCount(add + j, addEnd);
					if(!si != !sj)
						break;
					len += si > sj ? si : sj;
					i += si;
					j += sj;
					assert(rem + i <= remEnd);
					assert(add + j <= addEnd);
				}
			}

			if(len > best.len_) {
				best.rem_ = rem;
				best.remEnd_ = rem + i;
				best.add_ = add;
				best.addEnd_ = add + j;
				best.len_ = len;
			}

			add++;
		}
		rem++;
		add = bSave;
	}

	return best;
}

MatchList
DiffParser::compareBlocks(const char *remStart, const char *remEnd, const char *addStart, const char *addEnd)
{
	MatchList list;
	Match longest = longestMatch(remStart, remEnd, addStart, addEnd);
	if(longest.len_) {
		if(remStart < longest.rem_ && addStart < longest.add_)
			list = compareBlocks(remStart, longest.rem_, addStart, longest.add_);
		list.push_back(longest);

		if(longest.remEnd_ < remEnd && longest.addEnd_ < addEnd) {
			MatchList tail = compareBlocks(longest.remEnd_, remEnd, longest.addEnd_, addEnd);
			list.insert(list.end(), tail.begin(), tail.end());
		}
	}

	return list;
}

void
DiffParser::printBlock(const char id, const char *block, const char *blockEnd)
{
	bool newLine = block == buf_ || *(block - 1) == '\n';
	while(block < blockEnd) {
		if(newLine)
			app->printChar(id);
		newLine = *block == '\n';
		app->printChar(*block++);
	}
}

void
DiffParser::processBlock()
{
	inBlock_ = false;

	if(!blockAdd_) {
		app->setColor(colorLineDel);
		printBlock('-', blockRem_, line_);
		blockRem_ = nullptr;
		return;
	}
	if(!blockRem_) {
		app->setColor(colorLineAdd);
		printBlock('+', blockAdd_, line_);
		blockAdd_ = nullptr;
		return;
	}

	MatchList blocks = compareBlocks(blockRem_, blockAdd_, blockAdd_, line_);

	app->setColor(colorLineDel);
	const char *start = blockRem_;
	for(MatchList::iterator i = blocks.begin(); i != blocks.end(); ++i) {
		app->setHighlight(highlightOn);
		printBlock('-', start, i->rem_);
		app->setHighlight(highlightOff);
		printBlock('-', i->rem_, i->remEnd_);
		start = i->remEnd_;
	}
	app->setHighlight(highlightOn);
	printBlock('-', start, blockAdd_);

	app->setColor(colorLineAdd);
	start = blockAdd_;
	for(MatchList::iterator i = blocks.begin(); i != blocks.end(); ++i) {
		app->setHighlight(highlightOn);
		printBlock('+', start, i->add_);
		app->setHighlight(highlightOff);
		printBlock('+', i->add_, i->addEnd_);
		start = i->addEnd_;
	}
	app->setHighlight(highlightOn);
	printBlock('+', start, line_);
}

/*!
 * \brief Print \p (part of) current line and update internal line pointer and length. Ansi sequences will not be printed.
 * \param length how many bytes to print (-1 for whole line)
 */
void
DiffParser::printLineNoAnsi(int length/* = -1 */)
{
	if(length == -1 || length > lineLen_)
		length = lineLen_;

	if(!length)
		return;

	lineLen_ -= length;

	const char *lineEnd = line_ + length;

	while(line_ < lineEnd) {
		if(*line_ == '\33') { // skip ansi chars
			while(line_ < lineEnd && *line_++ != 'm');
			continue;
		}
		app->printChar(*line_++);
	}
}

void
DiffParser::handleFileInfoLine()
{
	// print '---' and '+++' lines

	app->setHighlight(highlightOff);
	app->setColor(colorFileInfo);

	printLineNoAnsi();

	app->setColor(colorReset);
	app->setHighlight(highlightReset);
}

void
DiffParser::handleRangeInfoLine()
{
	// print '@@' lines

	app->setHighlight(highlightOff);

	app->setColor(colorBlockRange);
	int at = 0;
	int rangeLen = 0;
	while(at < 4 && rangeLen < lineLen_) {
		if(line_[rangeLen++] == '@')
			at++;
	}
	printLineNoAnsi(rangeLen);

	app->setColor(colorBlockHeading);
	printLineNoAnsi();

	app->setColor(colorReset);
	app->setHighlight(highlightReset);
}

void
DiffParser::handleContextLine()
{
	// print ' ' lines inside diff block

	app->setHighlight(highlightOff);
	app->setColor(colorLineContext);

	printLineNoAnsi();

	app->setColor(colorReset);
	app->setHighlight(highlightReset);
}

void
DiffParser::handleRemLine()
{
	// handle '-' lines inside diff block

	if(blockAdd_) // when '-' block comes after '+' block, we have to process
		processBlock();

	if(!blockRem_)
		blockRem_ = line_;

	stripLineAnsi(1);

	// we are just preparing block buffers, they will be printed in processBlock()
}

void
DiffParser::handleAddLine()
{
	// handle '+' lines inside diff block

	if(!blockAdd_)
		blockAdd_ = line_;

	stripLineAnsi(1);

	// we are just preparing block buffers, they will be printed in processBlock()
}

void
DiffParser::handleGenericLine()
{
	app->setColor(colorReset);
	app->setHighlight(highlightReset);
	app->printAnsiCodes();

	while(lineLen_--)
		app->printChar(*line_++, false);
}
