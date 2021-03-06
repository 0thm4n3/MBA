/*
 *  MBA Tracer implementation
 *
 *  Copyright (c)   2016 Chiawei Wang
 *                  2016 ChongKuan Chen
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include <stdbool.h>

#include "tracer.h"
#include "tracer-priv.h"
#include "utlist.h"
#include "qemu-common.h"
#include "include/exec/cpu-common.h"
#include "exec/cpu-all.h"
#include "disas/disas.h"

#define MASK_KERN_ADDR 0xffff000000000000

// Inst Trace Linked-List Head
tracer_cb_record *g_process_tracer_head =NULL;
tracer_cb_record *g_universal_kernel_tracer_head =NULL;
tracer_cb_record *g_universal_tracer_head =NULL;

tracer_cb_record *g_process_btracer_head =NULL;
tracer_cb_record *g_universal_kernel_btracer_head =NULL;
tracer_cb_record *g_universal_btracer_head =NULL;

int g_serial_num = 0;

int g_error_no = 0;

void show_tracer_list_info( tracer_cb_record* head );

/// Check if the given address is in kernel space
/// NOTE that the check is based on the memory layout of Windows 10 x64 
inline bool tracer_is_kern_addr( uint64_t addr ) {
    return ((addr & MASK_KERN_ADDR) == MASK_KERN_ADDR);
}

inline int enable_tracer_in_list(tracer_cb_record* head, int uid)
{
    tracer_cb_record* rec = NULL;

    LL_FOREACH(head, rec){
        if(uid == rec->uid){
            rec->enabled = 1;
            return 0;
        }
    }
    return -1;
}

int tracer_enable_tracer(int uid)
{
    
    if( enable_tracer_in_list( g_process_tracer_head, uid)== 0) return 0;
    if( enable_tracer_in_list( g_universal_kernel_tracer_head, uid)== 0) return 0;
    if( enable_tracer_in_list( g_universal_tracer_head, uid)== 0) return 0;

    if( enable_tracer_in_list( g_process_btracer_head, uid)== 0) return 0;
    if( enable_tracer_in_list( g_universal_kernel_btracer_head, uid)== 0) return 0;
    if( enable_tracer_in_list( g_universal_btracer_head, uid)== 0) return 0;
    printf("enable tracer\n");

    g_error_no = TRACER_INVALID_ID;
    return -1;
}

inline int disable_tracer_in_list(tracer_cb_record* head, int uid)
{
    tracer_cb_record* rec = NULL;

    LL_FOREACH(head, rec){
        if(uid == rec->uid){
            rec->enabled = 0;
            return 0;
        }
    }
    return -1;
}



int tracer_disable_tracer(int uid)
{

    if( disable_tracer_in_list( g_process_tracer_head, uid)== 0) return 0;
    if( disable_tracer_in_list( g_universal_kernel_tracer_head, uid)== 0) return 0;
    if( disable_tracer_in_list( g_universal_tracer_head, uid)== 0) return 0;

    if( disable_tracer_in_list( g_process_btracer_head, uid)== 0) return 0;
    if( disable_tracer_in_list( g_universal_kernel_btracer_head, uid)== 0) return 0;
    if( disable_tracer_in_list( g_universal_btracer_head, uid)== 0) return 0;
    //printf("enable tracer\n");

    g_error_no = TRACER_INVALID_ID;
    return -1;

}

void* default_callback(void* env_state, uint64_t pc_start, uint64_t pc_end)
{
    X86CPU *cpu = X86_CPU(env_state);
    CPUX86State *env = &cpu->env;
    target_disas_inst_count( stdout, env, pc_start, 100, 1, 2);
    printf("\teax %08lx\t", env->regs[R_EAX]);
    printf("ebx %08lx\t", env->regs[R_EBX]);
    printf("ecx %08lx\t", env->regs[R_ECX]);
    printf("edx %08lx\n", env->regs[R_EDX]);
    printf("\tcr3 %08lx\n", env->cr[3]);
    printf("\n");
    return NULL;
}

static int add_tracer_internal( target_ulong cr3, const char* label, bool is_kernel, int trace_granularity, void*(*cb) (void*, uint64_t, uint64_t) ) {

    if(trace_granularity!=TRACER_GRANULARITY_INSTR && trace_granularity!=TRACER_GRANULARITY_CODEBLOCK)
    {
        g_error_no = TRACER_INVALID_GRANULARITY;
        return -1;
    }

    tracer_cb_record* tracer_rec = (tracer_cb_record *) calloc(1, sizeof(tracer_cb_record));
    //Add management for uid
    tracer_rec-> uid = ++g_serial_num ;
    strncpy( tracer_rec->label, label, MAX_SZ_TRACER_LABEL );
    //tracer_rec->enabled = true;
    tracer_rec->cr3 = cr3;
    tracer_rec->is_kernel_trace = is_kernel;
    tracer_rec->trace_granularity = trace_granularity;
    tracer_rec->is_universal = (tracer_rec->cr3 == 0)? true : false;
    tracer_rec->cb_func = cb ;

    // Add tracer to instruction list
    if(tracer_rec->trace_granularity == TRACER_GRANULARITY_INSTR)
    {    
        if( tracer_rec->cr3 !=0)
        {
            LL_APPEND(g_process_tracer_head, tracer_rec);
        }
        if( tracer_rec->cr3 == 0 && tracer_rec->is_kernel_trace)
        {
            LL_APPEND(g_universal_kernel_tracer_head, tracer_rec);
        }
        if( tracer_rec->cr3 == 0 && !tracer_rec->is_kernel_trace)
        {
            LL_APPEND(g_universal_tracer_head, tracer_rec);
        }
    }

    // Add tracer to block tracer list
    if(tracer_rec->trace_granularity == TRACER_GRANULARITY_CODEBLOCK)
    {
        if( tracer_rec->cr3 !=0)
        {
            LL_APPEND(g_process_btracer_head, tracer_rec);
        }
        if( tracer_rec->cr3 == 0 && tracer_rec->is_kernel_trace)
        {
            LL_APPEND(g_universal_kernel_btracer_head, tracer_rec);
        }
        if( tracer_rec->cr3 == 0 && !tracer_rec->is_kernel_trace)
        {
            LL_APPEND(g_universal_btracer_head, tracer_rec);
        }
    }

    tracer_rec->enabled = 0;

    printf("add new inst tracer\n");
    return tracer_rec-> uid; 
}


int tracer_add_inst_tracer( target_ulong cr3, const char* label, bool is_kernel, void*(*cb) (void*, uint64_t, uint64_t) ) {
    int tracer_id = -1;
    if(cb == NULL) 
        tracer_id = add_tracer_internal(cr3, label, is_kernel, TRACER_GRANULARITY_INSTR, &default_callback);
    else
        tracer_id = add_tracer_internal(cr3, label, is_kernel, TRACER_GRANULARITY_INSTR, cb);
    return tracer_id;
}

int tracer_add_block_tracer( target_ulong cr3, const char* label, bool is_kernel, void*(*cb) (void*, uint64_t, uint64_t) ) { 
    int tracer_id = -1;
    if(cb == NULL) 
        tracer_id = add_tracer_internal(cr3, label, is_kernel, TRACER_GRANULARITY_CODEBLOCK, &default_callback);
    else
        tracer_id = add_tracer_internal(cr3, label, is_kernel, TRACER_GRANULARITY_CODEBLOCK, cb);
    return tracer_id;
}

void show_tracer_list_info( tracer_cb_record* head ){
    tracer_cb_record* rec = NULL;
    LL_FOREACH( head, rec)
    {
        printf("\tuid: %d\t", rec->uid);
        printf("cr3: %lx\t", rec->cr3);
        printf("lable: %s\t", rec->label);
        printf("enable: %d\t", rec->enabled);
        printf("cb: %p\n", rec->cb_func);
    }

}

void tracer_list_callback(void){
    
    printf("=================Instruction Tracer============================\n");
    show_tracer_list_info(g_process_tracer_head);
    show_tracer_list_info(g_universal_tracer_head);
    show_tracer_list_info(g_universal_kernel_tracer_head);

    printf("=================Code Block Tracer============================\n");
    show_tracer_list_info(g_process_btracer_head);
    show_tracer_list_info(g_universal_btracer_head);
    show_tracer_list_info(g_universal_kernel_btracer_head);

}

int get_error_no(void){
    return g_error_no;
}
