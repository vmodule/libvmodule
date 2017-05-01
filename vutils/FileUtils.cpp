
#include <stdarg.h>
#include <assert.h>
#include <math.h>
#include <functional>
#include <algorithm>
#include <string.h>
#include <vutils/Logger.h>
#include <vutils/error.h>
#include <vutils/FileUtils.h>

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "FileUtils"

#ifdef DEBUG_ENABLE
#define MY_LOGD(fmt, arg...)  XLOGD(LOG_TAG,fmt, ##arg)//MY_LOGD(fmt, ##arg)X
#define MY_LOGE(fmt, arg...)  XLOGE(LOG_TAG,fmt, ##arg)//MY_LOGD(fmt, ##arg)X
#else
#define MY_LOGD(fmt, arg...)
#define MY_LOGE(fmt, arg...)  XLOGE(LOG_TAG,fmt, ##arg)//MY_LOGD(fmt, ##arg)X
#endif

namespace vmodule {

static status_t do_mkdir(const char *path, uint8_t mode) {

	struct stat st;
	if (0 != stat(path, &st)) {
		//  Directory does not exist.
		if (0 != mkdir(path, mode) && EEXIST != errno) {
			MY_LOGD("fail to do_mkdir [%s]: %d[%s]",
					path, errno, strerror(errno));
			return BAD_VALUE;
		}
	} else if (!S_ISDIR(st.st_mode)) {
		MY_LOGD("!S_ISDIR");
		return BAD_VALUE;
	}
	//
	return OK;
}

status_t make_dir(const char *path, uint8_t mode) {
	bool ret = true;
	char* copypath = strdup(path);
	char*pp = copypath;
	char*sp;
	while (ret && 0 != (sp = strchr(pp, '/'))) {
		if (sp != pp) {
			*sp = '\0';
			ret = do_mkdir(copypath, mode);
			*sp = '/';
		}
		pp = sp + 1;
	}
	if (ret)
		ret = do_mkdir(path, mode);
	free(copypath);
	return ret;
}

status_t delete_dir(const char * dirname) {
	char chBuf[256];
	DIR * dir = NULL;
	struct dirent *ptr;
	int ret = 0;
	dir = opendir(dirname);
	if (NULL == dir) {
		return -1;
	}
	while ((ptr = readdir(dir)) != NULL) {
		if (strcmp(ptr->d_name, ".") == 0 || strcmp(ptr->d_name, "..") == 0) {
			continue;
		}
		snprintf(chBuf, 256, "%s/%s", dirname, ptr->d_name);
		if (is_dir(chBuf)) {
			ret = delete_dir(chBuf);
			if (0 != ret) {
				return UNKNOWN_ERROR;
			}
		} else {
			ret = remove(chBuf);
			if (0 != ret) {
				return UNKNOWN_ERROR;
			}
		}
	}
	closedir(dir);
	ret = remove(dirname);
	if (0 != ret) {
		return UNKNOWN_ERROR;
	}
	return OK;
}

bool is_dir(char* filename) {
	struct stat buf;
	int ret = stat(filename, &buf);
	if (0 == ret) {
		if (buf.st_mode & S_IFDIR) {
			return true; //folder
		} else {
			return false; //file
		}
	}
	return false;
}

bool file_exist(const char * filename) {
	if (NULL == filename)
		return false;
	if (access(filename, F_OK) == 0)
		return true;
	else
		return false;
}

int file_size(const char * filename) {
	struct stat statbuf;
	int result = 0;
	result = stat(filename, &statbuf);
	if (result != 0)
		return 0;
	else
		return statbuf.st_size; //if 0 is -> dont have any config
}

size_t writeBufferToFile(const char* fname, uint8_t *buf, size_t size) {
	int nw, cnt = 0;
	size_t written = 0;

	MY_LOGD("opening file [%s]", fname);
	int fd = open(fname, O_RDWR | O_CREAT | O_TRUNC, S_IRWXU);
	if (fd < 0) {
		MY_LOGE( "failed to create file [%s]: %s", fname, strerror(errno));
		return 0;
	}

	MY_LOGD("writing %d bytes to file [%s]", size, fname);
	while (written < size) {
		nw = write(fd, buf + written, size - written);
		if (nw < 0) {
			MY_LOGE( "failed to write to file [%s]: %s",
					fname, strerror(errno));
			break;
		}
		written += nw;
		cnt++;
	}
	MY_LOGD("done writing %d bytes to file [%s] in %d passes",
			size, fname, cnt);
	fdatasync(fd);
	close(fd);
	return written;
}

/******************************************************************************
 *	read file to buffer
 *******************************************************************************/
size_t readFileToBuffer(const char *fname, uint8_t* buf, size_t size) {
	int nr, cnt = 0;
	size_t readCnt = 0;

	MY_LOGD("opening file [%s]", fname);
	int fd = open(fname, O_RDONLY);
	if (fd < 0) {
		MY_LOGE( "failed to create file [%s]: %s", fname, strerror(errno));
		return readCnt;
	}
	//
	if (size == 0) {
		size = lseek(fd, 0, SEEK_END);
		lseek(fd, 0, SEEK_SET);
	}
	//
	MY_LOGD("read %d bytes from file [%s]", size, fname);
	while (readCnt < size) {
		nr = read(fd, buf + readCnt, size - readCnt);
		if (nr < 0) {
			MY_LOGE( "failed to read from file [%s]: %s",
					fname, strerror(errno));
			break;
		}
		readCnt += nr;
		cnt++;
	}
	MY_LOGD("done writing %d bytes to buf [%s] in %d passes", size, fname, cnt);
	close(fd);
	return readCnt;
}

}
