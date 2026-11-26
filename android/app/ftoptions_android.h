/* Android 构建的 FreeType 选项：沿用上游默认，再关掉全部外部压缩/辅助库依赖
 *（我们没有编译 zlib/png/bzip2/brotli/harfbuzz 模块）。/system/fonts 里的
 * TTF/OTF/TTC 都用不到这些。经 FT_CONFIG_OPTIONS_H 注入。 */
#ifndef ANDROID_FTOPTIONS_H
#define ANDROID_FTOPTIONS_H

#include <freetype/config/ftoption.h>

#undef FT_CONFIG_OPTION_USE_ZLIB
#undef FT_CONFIG_OPTION_USE_PNG
#undef FT_CONFIG_OPTION_USE_HARFBUZZ
#undef FT_CONFIG_OPTION_USE_BZIP2
#undef FT_CONFIG_OPTION_USE_BROTLI
#undef FT_CONFIG_OPTION_USE_LZW

#endif
