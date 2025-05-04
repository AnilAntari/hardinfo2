#ifndef __CONFIG_H__
#define __CONFIG_H__

#define VERSION			"@HARDINFO2_VERSION@"
#define RELEASE_YEAR		@HARDINFO2_RELEASE_YEAR@

#define HARDINFO2_ARCH		"@HARDINFO2_ARCH@"

#define ARCH			"ARCH_@HARDINFO2_ARCH@"
#define OS			"@HARDINFO2_OS@"
#define PLATFORM		OS "-" ARCH
#define KERNEL			""
#define HOSTNAME		""
#define ARCH_@HARDINFO2_ARCH@
#define PACK_REQ		"@PACK_REQ@"

#define LIBDIR			"@CMAKE_INSTALL_LIBDIR@"
#define LIBPREFIX		"@CMAKE_INSTALL_FULL_LIBDIR@/hardinfo2"
#define PREFIX			"@CMAKE_INSTALL_FULL_DATAROOTDIR@/hardinfo2"

#cmakedefine HARDINFO2_DEBUG	@HARDINFO2_DEBUG@
#cmakedefine CMAKE_BUILD_TYPE 	@CMAKE_BUILD_TYPE@
#cmakedefine HARDINFO2_LIBSOUP3 @HARDINFO2_LIBSOUP3@
#cmakedefine HARDINFO2_QT5      @HARDINFO2_QT5@
#cmakedefine HARDINFO2_VK       @HARDINFO2_VK@
#cmakedefine HARDINFO2_VK_WAYLAND @HARDINFO2_VK_WAYLAND@
#cmakedefine HARDINFO2_VK_X11   @HARDINFO2_VK_X11@

#define Release 1
#define ON 1
#define OFF 0

#if !defined(HARDINFO2_LIBSOUP3)
  #define HARDINFO2_LIBSOUP3 0
#endif
#if !defined(HARDINFO2_QT5)
  #define HARDINFO2_QT5 0
#endif

#if defined(HARDINFO2_DEBUG) && (HARDINFO2_DEBUG==1)
  #define DEBUG(msg,...) fprintf(stderr, "*** %s:%d (%s) *** " msg "\n", \
        	           __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__)
  #define RELEASE -1
#else
  #define DEBUG(msg,...)
  #if defined(CMAKE_BUILD_TYPE) && (CMAKE_BUILD_TYPE==Release)
    #define RELEASE 1
  #else
    #define RELEASE 0
  #endif
#endif	/* HARDINFO2_DEBUG */

#define HAS_LINUX_WE 1

#cmakedefine01 HAS_LIBSENSORS

#endif	/* __CONFIG_H__ */
