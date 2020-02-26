#Copyright(c) Facebook, Inc.and its affiliates.
#
#This source code is licensed under the MIT license found in the
#LICENSE file in the root directory of this source tree.

set(DynHandleSources DynHandleTest.cpp)

add_hermes_unittest(HermesDynHandleTest
  ${DynHandleSources}
  )

target_link_libraries(HermesDynHandleTest
  hermesVMRuntime
  hermesDynHandle
  )
