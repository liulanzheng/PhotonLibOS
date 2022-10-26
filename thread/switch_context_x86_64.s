/*
Copyright 2022 The Photon Authors

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

.globl _photon_switch_context
#if !defined( __APPLE__ ) && !defined( __FreeBSD__ )
.type  _photon_switch_context, @function
#endif
_photon_switch_context: //(void** rdi_to, void** rsi_from)
push    %rbp
mov     %rsp, (%rsi);   // rsi is `from`
mov     (%rdi), %rsp;   // rdi is `to`
pop     %rbp
ret;


.globl _photon_switch_context_defer
#if !defined( __APPLE__ ) && !defined( __FreeBSD__ )
.type  _photon_switch_context_defer, @function
#endif
_photon_switch_context_defer: //(void* rdi_arg, void (*rsi_defer)(void*),
                              // void** rdx_to, void** rcx_from)
push    %rbp
mov     %rsp, (%rcx);

.globl _photon_switch_context_defer_die
#if !defined( __APPLE__ ) && !defined( __FreeBSD__ )
.type  _photon_switch_context_defer_die, @function
#endif
_photon_switch_context_defer_die:   // (void* rdi_dying_th, void (*rsi_defer_die)(void*),
                                    //  void** rdx_to_th)
mov     (%rdx), %rsp;
pop     %rbp
jmp     *%rsi;  // call defer(arg/dying_th), and it will return to the caller


