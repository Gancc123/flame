<!--
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-09-04 15:20:04
 * @LastEditors: lwg
 * @LastEditTime: 2019-12-19 16:06:38
 -->
# Flame  

## 编译说明  

```bash
git clone http://115.156.135.251:7979/xxx/flame-sp.git

cd flame-sp

./do_cmake.sh #进行编译
```

其中,  

* `do_cmake.sh`使用`cmake`生成相关文件，并调用`ninja`或`cmake --build`进行编译  
* 第一次编译后，将生成`build`文件夹，所有编译过程中生成的文件都可以在`build`文件夹中被找到  
* 重新编译时，  
  * 通常可使用`ninja -C build`部分重新编译  
  * 利用`ninja -C build clean`可以清空所有生成文件  
  * 如果修改了一些编译配置项，可以删除`build`文件夹，然后重新`./do_cmake.sh`  
* With SPDK，  
  1. 编译SPDK，SPDK版本为v18.07
  2. 设置SPDK路径`export SPDK_ROOT=path/to/spdk_dir`  
  3. `./do_cmake -D WITH_SPDK=ON`  
  或 将`CMakeLists.txt`中的`option(WITH_SPDK, ..., OFF)`修改为`ON`。  
  (`WITH_SPDK`配置项将被保存在`build/CMakeCache.txt`中, 部分重新编译时可以直接使用`ninja -C build`)  

### 依赖项  

* Boost::system  #仅仅使用Boost的pool( _头文件为主_ )，对Boost的依赖不高
* aio  
* libmysqlclient-dev, libmysqlcppconn-dev  
* ibverbs  
* protobuf, grpc  
