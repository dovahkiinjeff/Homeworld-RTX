#ifndef HW_MODERN_WINDOWS_SPLASH_H
#define HW_MODERN_WINDOWS_SPLASH_H

#ifdef __cplusplus
extern "C" {
#endif

void hwWindowsSplashStart(void);
void hwWindowsSplashUpdate(const char *stage);
void hwWindowsSplashFinish(void);

#ifdef __cplusplus
}
#endif

#endif
