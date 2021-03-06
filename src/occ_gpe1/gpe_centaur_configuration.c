/* IBM_PROLOG_BEGIN_TAG                                                   */
/* This is an automatically generated prolog.                             */
/*                                                                        */
/* $Source: chips/p9/procedures/lib/pm/centaur_thermal_access.c $         */
/*                                                                        */
/* IBM CONFIDENTIAL                                                       */
/*                                                                        */
/* EKB Project                                                            */
/*                                                                        */
/* COPYRIGHT 2017                                                         */
/* [+] International Business Machines Corp.                              */
/*                                                                        */
/*                                                                        */
/* The source code for this program is not published or otherwise         */
/* divested of its trade secrets, irrespective of what has been           */
/* deposited with the U.S. Copyright Office.                              */
/*                                                                        */
/* IBM_PROLOG_END_TAG                                                     */
/**
 * @briefcentaur_thermal_access
 */

#include "gpe_centaur.h"
#include "ppe42_scom.h"
#include "pk.h"
#include "p9_misc_scom_addresses.h"
#include "mcs_firmware_registers.h"
#include "pba_firmware_constants.h"
#include "pba_register_addresses.h"
#include "centaur_register_addresses.h"
#include "ppe42_msr.h"
#include "occhw_pba_common.h"

// Which GPE controls the PBASLAVE
#define OCI_MASTER_ID_GPE1 1

// Power Bus Address bit that configures centaur for HOST/OCC P9=bit(38)
#define PBA_HOST_OCC_CFG 0x0000000002000000ull;


const uint32_t MCFGPR[OCCHW_NCENTAUR] =
{
    MCS_0_MCRSVDE,
    MCS_0_MCRSVDF,
    MCS_1_MCRSVDE,
    MCS_1_MCRSVDF,
    MCS_2_MCRSVDE,
    MCS_2_MCRSVDF,
    MCS_3_MCRSVDE,
    MCS_3_MCRSVDF
};

const uint32_t MCSYNC[OCCHW_NCENTAUR/2] =
{
    MCS_0_MCSYNC,
    MCS_1_MCSYNC,
    MCS_2_MCSYNC,
    MCS_3_MCSYNC
};

const uint32_t MCCHIFIR[OCCHW_NCENTAUR] =
{
    MCP_CHAN0_CHI_FIR,
    MCP_CHAN1_CHI_FIR,
    MCP_CHAN2_CHI_FIR,
    MCP_CHAN3_CHI_FIR,
    MCP_CHAN4_CHI_FIR,
    MCP_CHAN5_CHI_FIR,
    MCP_CHAN6_CHI_FIR,
    MCP_CHAN7_CHI_FIR
};

const uint32_t MCMCICFG1Q[OCCHW_NCENTAUR] =
{
    MCP_CHAN0_CHI_MCICFG1Q,
    MCP_CHAN1_CHI_MCICFG1Q,
    MCP_CHAN2_CHI_MCICFG1Q,
    MCP_CHAN3_CHI_MCICFG1Q,
    MCP_CHAN4_CHI_MCICFG1Q,
    MCP_CHAN5_CHI_MCICFG1Q,
    MCP_CHAN6_CHI_MCICFG1Q,
    MCP_CHAN7_CHI_MCICFG1Q
};

///////////////////////////////////////////////////////////////
// These are PPE specific PBA routines.
//////////////////////////////////////////////////////////////
int
gpe_pba_parms_create(GpePbaParms* parms,
                     int slave,
                     int write_ttype,
                     int write_tsize,
                     int read_ttype)
{
    pba_slvctln_t* slvctl, *mask;
    pba_slvrst_t* slvrst;
    pba_slvrst_t* slvrst_in_progress;
    uint64_t all1 = 0xffffffffffffffffull;

    parms->slave_id = slave;

    slvctl = &(parms->slvctl);
    mask = &(parms->mask);
    slvrst = &(parms->slvrst);
    slvrst_in_progress = &(parms->slvrst_in_progress);

    parms->slvctl_address = PBA_SLVCTLN(slave);

    slvrst->value = 0;
    slvrst->fields.set = PBA_SLVRST_SET(slave);

    slvrst_in_progress->value = 0;
    slvrst_in_progress->fields.in_prog = PBA_SLVRST_IN_PROG(slave);

    slvctl->value = 0;
    mask->value = 0;

    slvctl->fields.enable = 1;
    mask->fields.enable = all1;

    slvctl->fields.mid_match_value = OCI_MASTER_ID_GPE1;
    mask->fields.mid_match_value = all1;

    slvctl->fields.mid_care_mask = all1;
    mask->fields.mid_care_mask = all1;

    slvctl->fields.write_ttype = write_ttype;
    mask->fields.write_ttype = all1;

    slvctl->fields.write_tsize = write_tsize;
    mask->fields.write_tsize = all1;

    slvctl->fields.read_ttype = read_ttype;
    mask->fields.read_ttype = all1;

    slvctl->fields.buf_alloc_a = 1;
    slvctl->fields.buf_alloc_b = 1;
    slvctl->fields.buf_alloc_c = 1;
    slvctl->fields.buf_alloc_w = 1;
    mask->fields.buf_alloc_a = 1;
    mask->fields.buf_alloc_b = 1;
    mask->fields.buf_alloc_c = 1;
    mask->fields.buf_alloc_w = 1;

    if (read_ttype == PBA_READ_TTYPE_CI_PR_RD)
    {

        slvctl->fields.buf_invalidate_ctl = 1;
        mask->fields.buf_invalidate_ctl = all1;

        slvctl->fields.read_prefetch_ctl = PBA_READ_PREFETCH_NONE;
        mask->fields.read_prefetch_ctl = all1;

    }
    else
    {

        slvctl->fields.buf_invalidate_ctl = 0;
        mask->fields.buf_invalidate_ctl = all1;
    }

    mask->value = ~(mask->value);

    return 0;
}


////////////////////////////////////////////////
// Centaur specific routines
////////////////////////////////////////////////
int gpe_centaur_configuration_create(CentaurConfiguration_t* o_config)
{
    int rc = 0;
    unsigned int i  = 0;
    mcfgpr_t   mcfgpr;
    uint64_t    bar = 0;
    uint64_t    mask = 0;
    uint64_t*   ptr = (uint64_t*)o_config;
    int designated_sync = -1;

    // Prevent unwanted interrupts from scom errors
    const uint32_t orig_msr = mfmsr() & MSR_SEM;
    mtmsr((orig_msr & ~(MSR_SIBRC | MSR_SIBRCA)) | MSR_SEM);

    for(i = 0; i < sizeof(CentaurConfiguration_t) / 8; ++i)
    {
        *ptr++ = 0ull;
    }

    o_config->configRc = CENTAUR_NOT_CONFIGURED;

    do
    {
        // Create the PBASLV configurations for the GPE procedures.
        // The 'dataParms' define the PBASLV setup needed to access the
        // Centaur sensor cache.  The 'scomParms' define the PBASLV setup
        // needed to access the Centaur SCOMs.

        rc = gpe_pba_parms_create(&(o_config->dataParms),
                                  PBA_SLAVE_CENTAUR,
                                  PBA_WRITE_TTYPE_CI_PR_W,
                                  PBA_WRITE_TTYPE_DC,
                                  PBA_READ_TTYPE_CL_RD_NC);

        if (rc)
        {
            rc = CENTAUR_DATA_SETUP_ERROR;
            break;
        }

        rc = gpe_pba_parms_create(&(o_config->scomParms),
                                  PBA_SLAVE_CENTAUR,
                                  PBA_WRITE_TTYPE_CI_PR_W,
                                  PBA_WRITE_TTYPE_DC,
                                  PBA_READ_TTYPE_CI_PR_RD);

        if (rc)
        {
            rc = CENTAUR_SCOM_SETUP_ERROR;
            break;
        }

        // Iterate through each MCS on the chip and check configuration.

        // Note that the code uniformly treats SCOM failures of the MCFGPR
        // registers as an unconfigured Centaur. This works both for real 
        // hardware,  as well as for our VBU models where some of the "valid"
        // MCS are not in the simulation models.

        for (i = 0; i < OCCHW_NCENTAUR; ++i)
        {
            // check for channel checkstop
            rc = check_channel_chkstp(i);
            if (rc)
            {
                // If scom failed OR there is a channel checkstop then
                // Centaur is not usable.
                rc = 0;
                continue;
            }

            // Verify that inband scom has been setup. If not then
            // assume the centaur is either non-existant or not configured.
            // Setup is provided by HWP p9c_set_inband_addr.C
            rc = getscom_abs(MCFGPR[i], &(mcfgpr.value));

            if (rc)
            {
                // ignore if can't be scomed.
                rc = 0;
                continue;
            }

            // If inband scom is not configured then assume the centaur does not exist
            if (!mcfgpr.fields.mcfgprq_valid)
            {
                continue;
            }


            // The 31-bit base-address (inband scom BAR) corresponds to bits [8:38] in the
            // 64-bit PowerBus address.
            // Set the HOST/OCC bit in the address.
            o_config->baseAddress[i] =
                ((uint64_t)(mcfgpr.fields.mcfgprq_base_address) << 25) | PBA_HOST_OCC_CFG;

            PK_TRACE_DBG("Centar[%d] Base Address: %016llx",i,o_config->baseAddress[i]);

            // Add the Centaur to the configuration
            o_config->config |= (CHIP_CONFIG_MCS(i) | CHIP_CONFIG_CENTAUR(i));
        }

        if (rc)
        {
            break;
        }

        // Find the designated sync
        for (i = 0; i < (OCCHW_NCENTAUR/2); ++i)
        {
            uint64_t mcsync;
            rc = getscom_abs(MCSYNC[i], &mcsync);
            if (rc)
            {
                PK_TRACE("getscom failed on MCSYNC, rc = %d. The first configured MC will be the designated sync",rc);
                rc = 0;
            }
            if (mcsync != 0)
            {
                designated_sync = i;
                // There can only be one sync, so stop searching.
                break;
            }
        }

        if (designated_sync < 0)
        {
            designated_sync = cntlz32(o_config->config << CHIP_CONFIG_MCS_BASE);
            PK_TRACE("No designated sync found, using MCS(%d)",designated_sync);
        }

        o_config->mcSyncAddr = MCSYNC[designated_sync];


        // Configure the PBA BAR and PBA BARMSK.
        // Set the BARMSK bits such that:
        // -PBA[8:22] are provided by the PBABAR.
        // -PBA[23:36] are provided by the PBASLVCTL ExtrAddr field
        // -PBA[37:43] are provided by the OCI addr[5:11]
        // PBA[44:63] will always come from the OCI addr[12:31]
        // Note: This code should no longer be needed when the BAR/BARMSK is set
        // by PHYP.
        if (o_config->config != 0)
        {
            uint64_t bar = 0;
            uint64_t barMsk = PBA_BARMSKN_MASK_MASK;

            for (i = 0; i < OCCHW_NCENTAUR; ++i)
            {
                bar |= o_config->baseAddress[i];
            }

            bar &= ~barMsk;

            PK_TRACE_DBG("PBABAR(%d): %016llx", PBA_BAR_CENTAUR, bar);
            PK_TRACE_DBG("PBABARMSK: %016llx", barMsk);

            rc = putscom_abs(PBA_BARMSKN(PBA_BAR_CENTAUR), barMsk);

            if (rc)
            {
                PK_TRACE_DBG("Unexpected rc = 0x%08x SCOMing PBA_BARMSKN(%d)\n",
                             (uint32_t)rc, PBA_BAR_CENTAUR);
                rc = CENTAUR_BARMSKN_PUTSCOM_FAILURE;
                break;
            }

            rc = putscom_abs(PBA_BARN(PBA_BAR_CENTAUR), bar);
            if (rc)
            {
                PK_TRACE_DBG("Unexpected rc = 0x%08x SCOMing PBA_BARN(%d)\n",
                             (uint32_t)rc, PBA_BAR_CENTAUR);
                rc = CENTAUR_BARN_PUTSCOM_FAILURE;
                break;
            }
        }

        // Do an independent check that every Centaur base address
        // can be generated by the combination of the current BAR and
        // BAR Mask, along with the initial requirement that the mask must
        // include at least bits 38:43.

        if (o_config->config != 0)
        {
            rc = getscom_abs(PBA_BARN(PBA_BAR_CENTAUR), &bar);

            if (rc)
            {
                PK_TRACE_DBG("Unexpected rc = 0x%08x SCOMing PBA_BARN(%d)\n",
                             (uint32_t)rc, PBA_BAR_CENTAUR);
                rc = CENTAUR_BARN_GETSCOM_FAILURE;
                break;
            }

            rc = getscom_abs(PBA_BARMSKN(PBA_BAR_CENTAUR), &mask);

            if (rc)
            {
                PK_TRACE_DBG("Unexpected rc = 0x%08x SCOMing PBA_BARMSKN(%d)\n",
                             (uint32_t)rc, PBA_BAR_CENTAUR);
                rc = CENTAUR_BARMSKN_GETSCOM_FAILURE;
                break;
            }

            bar = bar & PBA_BARN_ADDR_MASK;
            mask = mask & PBA_BARMSKN_MASK_MASK;

            if ((mask & 0x0000000003f00000ull) != 0x0000000003f00000ull)
            {

                PK_TRACE("PBA BAR mask (%d) does not cover bits 38:43\n", PBA_BAR_CENTAUR);
                rc = CENTAUR_MASK_ERROR;
                break;
            }

            for (i = 0; i < OCCHW_NCENTAUR; ++i)
            {
                if (o_config->baseAddress[i] != 0)
                {
                    if ((o_config->baseAddress[i] & ~mask) !=
                        (bar & ~mask))
                    {

                        PK_TRACE("BAR/Mask (%d) error for MCS/Centaur %d",
                                 PBA_BAR_CENTAUR, i);

                        PK_TRACE("    base = 0x%08x%08x",
                                 (uint32_t)(o_config->baseAddress[i]>>32),
                                 (uint32_t)(o_config->baseAddress[i]));

                        PK_TRACE("    bar  = 0x%08x%08x"
                                 "    mask = 0x%08x%08x",
                                 (uint32_t)(bar >> 32),
                                 (uint32_t)(bar),
                                 (uint32_t)(mask >> 32),
                                 (uint32_t)(mask));

                        rc = CENTAUR_BAR_MASK_ERROR;
                        break;
                    }
                }
            }

            if (rc)
            {
                break;
            }
        }


        // At this point the structure is initialized well-enough that it can
        // be used by gpe_scom_centaur().


        o_config->configRc = 0;

        if (o_config->config == 0)
        {
            break;
        }


        // Get Device ID from each centaur
        centaur_get_scom_vector(o_config,
                                CENTAUR_DEVICE_ID,
                                (uint64_t*)(&(o_config->deviceId[0])));

    }
    while(0);

    o_config->configRc = rc;

    mtmsr(orig_msr);

    return rc;
}

int check_channel_chkstp(unsigned int i_centaur)
{
    int rc = 0;
    mcchifir_t chifir;
    mcmcicfg_t chicfg;

    do
    {
        rc = getscom_abs(MCCHIFIR[i_centaur], &(chifir.value));
        if (rc)
        {
            PK_TRACE("MCCHIFIR scom failed. rc = %d",rc);
            break;
        }

        if(chifir.fields.fir_dsrc_no_forward_progress ||
           chifir.fields.fir_dmi_channel_fail  ||
           chifir.fields.fir_channel_init_timeout ||
           chifir.fields.fir_channel_interlock_err ||
           chifir.fields.fir_replay_buffer_ue ||
           chifir.fields.fir_replay_buffer_overrun ||
           chifir.fields.fir_df_sm_perr ||
           chifir.fields.fir_cen_checkstop ||
           chifir.fields.fir_dsff_tag_overrun ||
           chifir.fields.fir_dsff_mca_async_cmd_error ||
           chifir.fields.fir_dsff_seq_error ||
           chifir.fields.fir_dsff_timeout)
        {
            PK_TRACE("MCCHIFIR: %08x%08x for channel %d",
                     chifir.words.high_order,
                     chifir.words.low_order,
                     i_centaur);

            rc = getscom_abs(MCMCICFG1Q[i_centaur], &(chicfg.value));
            if (rc)
            {
                PK_TRACE("MCMCICFG scom failed. rc = %d",rc);
                break;
            }

            PK_TRACE("MCMCICFG1Q %08x%08x",
                chicfg.words.high_order,
                chicfg.words.low_order);

            rc = CENTAUR_CHANNEL_CHECKSTOP;
        }
    } while(0);

    return rc;
}
