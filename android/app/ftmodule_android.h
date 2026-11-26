/* Android 构建的 FreeType 模块注册表：只注册 TT/SFNT/PS 名称/灰度光栅化四件套，
 * 替换默认的全量列表（否则链接期找不到 type1/cid/pcf 等未编译模块的类）。
 * 经 FT_CONFIG_MODULES_H 注入，见 android/CMakeLists.txt。
 *
 * 注意：ftinit.c 会把这个文件 include 两次（一次取 extern 声明、一次生成
 * 模块表数组），所以这里**不能加 include guard** —— FreeType 自带的
 * ftmodule.h 也没有 guard。 */
FT_USE_MODULE( FT_Module_Class, sfnt_module_class )
FT_USE_MODULE( FT_Module_Class, tt_driver_class )
FT_USE_MODULE( FT_Module_Class, psnames_module_class )
FT_USE_MODULE( FT_Renderer_Class, ft_smooth_renderer_class )
