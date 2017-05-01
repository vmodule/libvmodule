/*
 * FileUtils.h
 *
 *  Created on: Jan 1, 2017
 *      Author: jeffrey
 */
#pragma once

#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <vector>
#include <cctype>
#include <iterator>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <utility>
#include <sys/types.h> 
#include <sys/stat.h>
#include <vutils/error.h>

namespace vmodule {
	status_t make_dir(const char * path, uint8_t mode);
	status_t delete_dir(const char * dirname);
	size_t writeBufferToFile(const char* fname, uint8_t *buf, size_t size);
	size_t readFileToBuffer(const char *fname, uint8_t* buf, size_t size);
	bool file_exist(const char * filename);
	int file_size(const char * filename);
	bool is_dir(char* filename);
}
