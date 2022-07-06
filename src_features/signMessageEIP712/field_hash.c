#ifdef HAVE_EIP712_FULL_SUPPORT

#include <stdlib.h>
#include "field_hash.h"
#include "encode_field.h"
#include "path.h"
#include "mem.h"
#include "mem_utils.h"
#include "shared_context.h"
#include "ui_logic.h"
#include "ethUtils.h" // KECCAK256_HASH_BYTESIZE
#include "context.h" // contract_addr
#include "utils.h" // u64_from_BE
#include "apdu_constants.h" // APDU response codes
#include "typed_data.h"
#include "commands_712.h"

static s_field_hashing *fh = NULL;

bool    field_hash_init(void)
{
    if (fh == NULL)
    {
        if ((fh = MEM_ALLOC_AND_ALIGN_TYPE(*fh)) == NULL)
        {
            apdu_response_code = APDU_RESPONSE_INSUFFICIENT_MEMORY;
            return false;
        }
        fh->state = FHS_IDLE;
    }
    return true;
}

void    field_hash_deinit(void)
{
    fh = NULL;
}

bool    field_hash(const uint8_t *data,
                   uint8_t data_length,
                   bool partial)
{
    const void *field_ptr;
    e_type field_type;
    uint8_t *value = NULL;
    const char *key;
    uint8_t keylen;
#ifdef DEBUG
    const char *type;
    uint8_t typelen;
#endif

    if (fh == NULL)
    {
        return false;
    }
    // get field by path
    if ((field_ptr = path_get_field()) == NULL)
    {
        apdu_response_code = APDU_RESPONSE_CONDITION_NOT_SATISFIED;
        return false;
    }
    key = get_struct_field_keyname(field_ptr, &keylen);
    field_type = struct_field_type(field_ptr);
    if (fh->state == FHS_IDLE) // first packet for this frame
    {
        fh->remaining_size = __builtin_bswap16(*(uint16_t*)&data[0]); // network byte order
        data += sizeof(uint16_t);
        data_length -= sizeof(uint16_t);
        fh->state = FHS_WAITING_FOR_MORE;
        if (IS_DYN(field_type))
        {
            cx_keccak_init(&global_sha3, 256); // init hash
            ui_712_new_field(field_ptr, data, data_length);
        }
    }
    fh->remaining_size -= data_length;
    // if a dynamic type -> continue progressive hash
    if (IS_DYN(field_type))
    {
        cx_hash((cx_hash_t*)&global_sha3,
                0,
                data,
                data_length,
                NULL,
                0);
    }
    if (fh->remaining_size == 0)
    {
        if (partial) // only makes sense if marked as complete
        {
            apdu_response_code = APDU_RESPONSE_INVALID_DATA;
            return false;
        }
#ifdef DEBUG
        PRINTF("=> ");
        type = get_struct_field_typename(field_ptr, &typelen);
        fwrite(type, sizeof(char), typelen, stdout);
        PRINTF(" ");
        key = get_struct_field_keyname(field_ptr, &keylen);
        fwrite(key, sizeof(char), keylen, stdout);
        PRINTF("\n");
#endif

        if (!IS_DYN(field_type))
        {
            switch (field_type)
            {
                case TYPE_SOL_INT:
                    value = encode_int(data, data_length, get_struct_field_typesize(field_ptr));
                    break;
                case TYPE_SOL_UINT:
                    value = encode_uint(data, data_length);
                    break;
                case TYPE_SOL_BYTES_FIX:
                    value = encode_bytes(data, data_length);
                    break;
                case TYPE_SOL_ADDRESS:
                    value = encode_address(data, data_length);
                    break;
                case TYPE_SOL_BOOL:
                    value = encode_boolean((bool*)data, data_length);
                    break;
                case TYPE_CUSTOM:
                default:
                    apdu_response_code = APDU_RESPONSE_INVALID_DATA;
                    PRINTF("Unknown solidity type!\n");
                    return false;
            }

            if (value == NULL)
            {
                return false;
            }
            ui_712_new_field(field_ptr, data, data_length);
        }
        else
        {
            if ((value = mem_alloc(KECCAK256_HASH_BYTESIZE)) == NULL)
            {
                apdu_response_code = APDU_RESPONSE_INSUFFICIENT_MEMORY;
                return false;
            }
            // copy hash into memory
            cx_hash((cx_hash_t*)&global_sha3,
                    CX_LAST,
                    NULL,
                    0,
                    value,
                    KECCAK256_HASH_BYTESIZE);
        }

        // TODO: Move elsewhere
        uint8_t len = IS_DYN(field_type) ?
                      KECCAK256_HASH_BYTESIZE :
                      EIP_712_ENCODED_FIELD_LENGTH;
        // last thing in mem is the hash of the previous field
        // and just before it is the current hash context
        cx_sha3_t *hash_ctx = (cx_sha3_t*)(value - sizeof(cx_sha3_t));
        // start the progressive hash on it
        cx_hash((cx_hash_t*)hash_ctx,
                0,
                value,
                len,
                NULL,
                0);
        // deallocate it
        mem_dealloc(len);

        if (path_get_root_type() == ROOT_DOMAIN)
        {
            // copy contract address into context
            if (strncmp(key, "verifyingContract", keylen) == 0)
            {
                if (data_length != sizeof(eip712_context->contract_addr))
                {
                    apdu_response_code = APDU_RESPONSE_INVALID_DATA;
                    PRINTF("Unexpected verifyingContract length!\n");
                    return false;
                }
                memcpy(eip712_context->contract_addr, data, data_length);
            }
            else if (strncmp(key, "chainId", keylen) == 0)
            {
                uint64_t chainId = u64_from_BE(data, data_length);

                if (chainId != chainConfig->chainId)
                {
                    apdu_response_code = APDU_RESPONSE_CONDITION_NOT_SATISFIED;
                    PRINTF("EIP712Domain chain ID mismatch, expected 0x%.*h, got 0x%.*h !\n",
                           sizeof(chainConfig->chainId),
                           &chainConfig->chainId,
                           sizeof(chainId),
                           &chainId);
                    return false;
                }
            }
        }
        path_advance();
        fh->state = FHS_IDLE;
        ui_712_finalize_field();
    }
    else
    {
        if (!partial || !IS_DYN(field_type)) // only makes sense if marked as partial
        {
            apdu_response_code = APDU_RESPONSE_INVALID_DATA;
            return false;
        }
        handle_eip712_return_code(true);
    }

    return true;
}

#endif // HAVE_EIP712_FULL_SUPPORT
