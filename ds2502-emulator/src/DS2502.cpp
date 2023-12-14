#include "DS2502.h"

DS2502::DS2502(uint8_t ID1, uint8_t ID2, uint8_t ID3, uint8_t ID4, uint8_t ID5, uint8_t ID6, uint8_t ID7) : OneWireItem(ID1, ID2, ID3, ID4, ID5, ID6, ID7)
{
    static_assert(MEM_SIZE < 256, "Implementation does not cover the whole address-space");

    clearStatus();
}

void DS2502::duty(OneWireHub *const hub)
{
    uint8_t reg_TA[2], cmd, data, crc = 0; // Target address, redirected address, command, data, crc

    if (hub->recv(&cmd))
        return;
    crc = crc8(&cmd, 1, crc);

    if (hub->recv(reg_TA, 2))
        return;
    crc = crc8(reg_TA, 2, crc);

    if (reg_TA[1] != 0)
        return; // upper byte of target address should not contain any data

    switch (cmd)
    {
    case 0xF0: // READ MEMORY

        if (hub->send(&crc))
            break;

        crc = 0; // reInit CRC and send data
        for (uint8_t i = reg_TA[0]; i < sizeof_memory; ++i)
        {
            const uint8_t reg_RA = translateRedirection(i);
            if (hub->send(&memory[reg_RA]))
                return;
            crc = crc8(&memory[reg_RA], 1, crc);
        }
        hub->send(&crc);
        break; // datasheet says we should return all 1s, send(255), till reset, nothing to do here, 1s are passive
    }
}

uint8_t DS2502::translateRedirection(const uint8_t source_address) const
{
    const uint8_t source_page = static_cast<uint8_t>(source_address >> 5);
    const uint8_t destin_page = getPageRedirection(source_page);
    if (destin_page == 0x00)
        return source_address;
    const uint8_t destin_address = (source_address & PAGE_MASK) | (destin_page << 5);
    return destin_address;
}

void DS2502::clearStatus(void)
{
    memset(status, static_cast<uint8_t>(0xFF), STATUS_SIZE);
    status[STATUS_FACTORYP] = 0x00; // last byte should be always zero
}

void DS2502::setPageUsed(const uint8_t page)
{
    if (page < PAGE_COUNT)
        status[STATUS_WP_PAGES] &= ~(uint8_t(1 << (page + 4)));
}

bool DS2502::setPageRedirection(const uint8_t page_source, const uint8_t page_destin)
{
    if (page_source >= PAGE_COUNT)
        return false; // really available
    if (page_destin >= PAGE_COUNT)
        return false; // virtual mem of the device

    status[page_source + STATUS_PG_REDIR] = (page_destin == page_source) ? uint8_t(0xFF) : ~page_destin; // datasheet dictates this, so no page can be redirected to page 0
    return true;
}

uint8_t DS2502::getPageRedirection(const uint8_t page) const
{
    if (page >= PAGE_COUNT)
        return 0x00;
    return ~(status[page + STATUS_PG_REDIR]); // TODO: maybe invert this in ReadStatus and safe some Operations? Redirection is critical and often done
}
