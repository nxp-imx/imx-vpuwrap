// Copyright 2019 NXP
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package vpu_wrapper

import (
        "android/soong/android"
        "android/soong/cc"
        "strings"
        "github.com/google/blueprint/proptools"
)

func init() {
    android.RegisterModuleType("vpu_wrapper_hantro_defaults", vpu_wrapper_hantroDefaultsFactory)
}

func vpu_wrapper_hantroDefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, vpu_wrapper_hantroDefaults)
    return module
}

func vpu_wrapper_hantroDefaults(ctx android.LoadHookContext) {
    var Cflags []string
    var Srcs []string
    var Shared_libs []string
    type props struct {
        Target struct {
                Android struct {
                        Enabled *bool
                        Cflags []string
                        Srcs []string
                        Shared_libs []string
                }
        }
    }
    p := &props{}
    var vpu_type string = ctx.Config().VendorConfig("IMXPLUGIN").String("BOARD_VPU_TYPE")
    if strings.Contains(vpu_type, "hantro") {
        p.Target.Android.Enabled = proptools.BoolPtr(true)
    } else {
        p.Target.Android.Enabled = proptools.BoolPtr(false)
    }
    var board string = ctx.Config().VendorConfig("IMXPLUGIN").String("BOARD_SOC_TYPE")
    if strings.Contains(board, "IMX8MM") {
        Cflags = append(Cflags, "-DHANTRO_VPU_ENC")
        Srcs = append(Srcs, "vpu_wrapper_hantro_encoder.c")
        Shared_libs = append(Shared_libs, "libcodec_enc")
        Shared_libs = append(Shared_libs, "libhantro_h1")
    } else if strings.Contains(board, "IMX8MP") {
        Cflags = append(Cflags, "-DHANTRO_VPU_ENC")
        Srcs = append(Srcs, "vpu_wrapper_hantro_VCencoder.c")
        Shared_libs = append(Shared_libs, "libhantro_vc8000e")
    }
    if ctx.Config().VendorConfig("IMXPLUGIN").String("CFG_SECURE_DATA_PATH") == "y" {
        Cflags = append(Cflags, "-DALWAYS_ENABLE_SECURE_PLAYBACK")
    }
    p.Target.Android.Cflags = Cflags
    p.Target.Android.Srcs = Srcs
    p.Target.Android.Shared_libs = Shared_libs
    ctx.AppendProperties(p)
}
