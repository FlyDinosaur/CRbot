#pragma once

// 把 stdout / stderr 同时写到控制台和日志文件。
// 做法：把它们重定向到一个匿名管道，由一个后台线程把字节同时写给
// 控制台和文件——这样 printf、std::cout、std::cerr 全都能被捕获，
// 不需要改动任何一处现有日志代码。
// 返回 false 表示没装成功（此时行为与原来完全一致，只是没有日志文件）。
bool start_log_tee(const char* path);
