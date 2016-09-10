
#include "qemu/osdep.h"
#include "cpu.h"
#include <stdint.h>
#include "panda/plugin.h"
#include "panda/callback_support.h"
#include "panda/common.h"

#include "panda/rr/rr_log.h"
#include "exec/cpu-common.h"



// These are used in exec.c
void panda_callbacks_before_dma(CPUState *cpu, hwaddr addr1, const uint8_t *buf, hwaddr l, int is_write) {
    if (rr_mode == RR_REPLAY) {
        panda_cb_list *plist;
        for (plist = panda_cbs[PANDA_CB_REPLAY_BEFORE_DMA];
             plist != NULL; plist = panda_cb_list_next(plist)) {
            plist->entry.replay_before_dma(cpu, is_write, (uint8_t *) buf, (uint64_t) addr1, l);
        }
    }
}

void panda_callbacks_after_dma(CPUState *cpu, hwaddr addr1, const uint8_t *buf, hwaddr l, int is_write) {
    if (rr_mode == RR_REPLAY) {
        panda_cb_list *plist;
       for (plist = panda_cbs[PANDA_CB_REPLAY_AFTER_DMA];
            plist != NULL; plist = panda_cb_list_next(plist)) {
            plist->entry.replay_after_dma(cpu, is_write, (uint8_t *) buf, (uint64_t) addr1, l);
        }
    }
}

// These are used in cpu-exec.c
void panda_callbacks_before_block_exec(CPUState *cpu, TranslationBlock *tb) {
    panda_cb_list *plist;
    for (plist = panda_cbs[PANDA_CB_BEFORE_BLOCK_EXEC];
         plist != NULL; plist = panda_cb_list_next(plist)) {
        plist->entry.before_block_exec(cpu, tb);
    }
}


void panda_callbacks_after_block_exec(CPUState *cpu, TranslationBlock *tb, TranslationBlock *next_tb) {
    panda_cb_list *plist;
    for (plist = panda_cbs[PANDA_CB_AFTER_BLOCK_EXEC];
         plist != NULL; plist = panda_cb_list_next(plist)) {
        plist->entry.after_block_exec(cpu, tb, next_tb);
    }
}


void panda_callbacks_before_block_translate(CPUState *cpu, target_ulong pc) {
    panda_cb_list *plist;
    for (plist = panda_cbs[PANDA_CB_BEFORE_BLOCK_TRANSLATE];
         plist != NULL; plist = panda_cb_list_next(plist)) {
        plist->entry.before_block_translate(cpu, pc);
    }
}


void panda_callbacks_after_block_translate(CPUState *cpu, TranslationBlock *tb) {
    panda_cb_list *plist;
    for (plist = panda_cbs[PANDA_CB_AFTER_BLOCK_TRANSLATE];
         plist != NULL; plist = panda_cb_list_next(plist)) {
        plist->entry.after_block_translate(cpu, tb);
    }
}

void panda_before_find_fast(void) {
    if (panda_plugin_to_unload){
        panda_plugin_to_unload = false;
        int i;
        for (i = 0; i < MAX_PANDA_PLUGINS; i++){
            if (panda_plugins_to_unload[i]){
                panda_do_unload_plugin(i);
                panda_plugins_to_unload[i] = false;
            }
        }
    }
    if (panda_flush_tb()) {
        tb_flush(first_cpu);
    }
}


bool panda_callbacks_after_find_fast(CPUState *cpu, TranslationBlock *tb, bool bb_invalidate_done) {
    panda_cb_list *plist;
    bool panda_invalidate_tb = false;
    if (unlikely(!bb_invalidate_done)) {
        for(plist = panda_cbs[PANDA_CB_BEFORE_BLOCK_EXEC_INVALIDATE_OPT];
            plist != NULL; plist = panda_cb_list_next(plist)) {
            panda_invalidate_tb |=
                plist->entry.before_block_exec_invalidate_opt(cpu, tb);
        }
        return true;
    }
    return false;
}


// These are used in target-i386/translate.c
bool panda_callbacks_insn_translate(CPUState *env, target_ulong pc) {
    panda_cb_list *plist;
    bool panda_exec_cb = false;
    for(plist = panda_cbs[PANDA_CB_INSN_TRANSLATE]; plist != NULL;
        plist = panda_cb_list_next(plist)) {
        panda_exec_cb |= plist->entry.insn_translate(env, pc);
    }
    return panda_exec_cb;
}


// These are used in softmmu_template.h
void panda_callbacks_before_mem_read(CPUState *env, target_ulong pc,
                                     target_ulong addr, uint32_t data_size) {
    panda_cb_list *plist;
    for(plist = panda_cbs[PANDA_CB_VIRT_MEM_BEFORE_READ]; plist != NULL;
        plist = panda_cb_list_next(plist)) {
        plist->entry.virt_mem_before_read(env, env->panda_guest_pc, addr,
                                          data_size);
    }
    hwaddr paddr = panda_virt_to_phys(env, addr);
    for(plist = panda_cbs[PANDA_CB_PHYS_MEM_BEFORE_READ]; plist != NULL;
        plist = panda_cb_list_next(plist)) {
        plist->entry.phys_mem_before_read(env, env->panda_guest_pc, paddr,
                                          data_size);
    }
}


void panda_callbacks_after_mem_read(CPUState *env, target_ulong pc,
                                    target_ulong addr, uint32_t data_size,
                                    uint64_t result) {
    panda_cb_list *plist;
    for(plist = panda_cbs[PANDA_CB_VIRT_MEM_AFTER_READ]; plist != NULL;
        plist = panda_cb_list_next(plist)) {
        plist->entry.virt_mem_after_read(env, env->panda_guest_pc, addr,
                                         data_size, &result);
    }
    hwaddr paddr = panda_virt_to_phys(env, addr);
    for(plist = panda_cbs[PANDA_CB_PHYS_MEM_AFTER_READ]; plist != NULL;
        plist = panda_cb_list_next(plist)) {
        plist->entry.phys_mem_after_read(env, env->panda_guest_pc, paddr,
                                         data_size, &result);
    }
}


void panda_callbacks_before_mem_write(CPUState *env, target_ulong pc,
                                      target_ulong addr, uint32_t data_size,
                                      uint64_t val) {
    panda_cb_list *plist;
    for(plist = panda_cbs[PANDA_CB_VIRT_MEM_BEFORE_WRITE]; plist != NULL;
        plist = panda_cb_list_next(plist)) {
        plist->entry.virt_mem_before_write(env, env->panda_guest_pc, addr,
                                           data_size, &val);
    }
    hwaddr paddr = panda_virt_to_phys(env, addr);
    for(plist = panda_cbs[PANDA_CB_PHYS_MEM_BEFORE_WRITE]; plist != NULL;
        plist = panda_cb_list_next(plist)) {
        plist->entry.phys_mem_before_write(env, env->panda_guest_pc, paddr,
                                           data_size, &val);
    }
}


void panda_callbacks_after_mem_write(CPUState *env, target_ulong pc,
                                     target_ulong addr, uint32_t data_size,
                                     uint64_t val) {
    panda_cb_list *plist;
    for(plist = panda_cbs[PANDA_CB_VIRT_MEM_AFTER_WRITE]; plist != NULL;
        plist = panda_cb_list_next(plist)) {
        plist->entry.virt_mem_after_write(env, env->panda_guest_pc, addr,
                                          data_size, &val);
    }
    hwaddr paddr = panda_virt_to_phys(env, addr);
    for(plist = panda_cbs[PANDA_CB_PHYS_MEM_AFTER_WRITE]; plist != NULL;
        plist = panda_cb_list_next(plist)) {
        plist->entry.phys_mem_after_write(env, env->panda_guest_pc, paddr,
                                          data_size, &val);
    }
}


// target-i386/misc_helpers.c
void panda_callbacks_cpuid(CPUState *env) {
    panda_cb_list *plist;
    for(plist = panda_cbs[PANDA_CB_GUEST_HYPERCALL]; plist != NULL; plist = panda_cb_list_next(plist)) {
        plist->entry.guest_hypercall(env);
    }
}


void panda_callbacks_cpu_restore_state(CPUState *env, TranslationBlock *tb) {
    panda_cb_list *plist;
    for(plist = panda_cbs[PANDA_CB_CPU_RESTORE_STATE]; plist != NULL;
        plist = panda_cb_list_next(plist)) {
        plist->entry.cb_cpu_restore_state(env, tb);
    }
}


void panda_callbacks_asid_changed(CPUState *env, target_ulong old_asid, target_ulong new_asid) {
    panda_cb_list *plist;
    for(plist = panda_cbs[PANDA_CB_ASID_CHANGED]; plist != NULL; plist = panda_cb_list_next(plist)) {
        plist->entry.asid_changed(env, old_asid, new_asid);
    }
}

