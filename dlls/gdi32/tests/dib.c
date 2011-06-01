/*
 * DIB driver tests.
 *
 * Copyright 2011 Huw Davies
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>
#include <stdio.h>
#include <math.h>

#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "wincrypt.h"

#include "wine/test.h"

static HCRYPTPROV crypt_prov;
static DWORD (WINAPI *pSetLayout)(HDC hdc, DWORD layout);

static const DWORD rop3[256] =
{
    0x000042, 0x010289, 0x020C89, 0x0300AA, 0x040C88, 0x0500A9, 0x060865, 0x0702C5,
    0x080F08, 0x090245, 0x0A0329, 0x0B0B2A, 0x0C0324, 0x0D0B25, 0x0E08A5, 0x0F0001,
    0x100C85, 0x1100A6, 0x120868, 0x1302C8, 0x140869, 0x1502C9, 0x165CCA, 0x171D54,
    0x180D59, 0x191CC8, 0x1A06C5, 0x1B0768, 0x1C06CA, 0x1D0766, 0x1E01A5, 0x1F0385,
    0x200F09, 0x210248, 0x220326, 0x230B24, 0x240D55, 0x251CC5, 0x2606C8, 0x271868,
    0x280369, 0x2916CA, 0x2A0CC9, 0x2B1D58, 0x2C0784, 0x2D060A, 0x2E064A, 0x2F0E2A,
    0x30032A, 0x310B28, 0x320688, 0x330008, 0x3406C4, 0x351864, 0x3601A8, 0x370388,
    0x38078A, 0x390604, 0x3A0644, 0x3B0E24, 0x3C004A, 0x3D18A4, 0x3E1B24, 0x3F00EA,
    0x400F0A, 0x410249, 0x420D5D, 0x431CC4, 0x440328, 0x450B29, 0x4606C6, 0x47076A,
    0x480368, 0x4916C5, 0x4A0789, 0x4B0605, 0x4C0CC8, 0x4D1954, 0x4E0645, 0x4F0E25,
    0x500325, 0x510B26, 0x5206C9, 0x530764, 0x5408A9, 0x550009, 0x5601A9, 0x570389,
    0x580785, 0x590609, 0x5A0049, 0x5B18A9, 0x5C0649, 0x5D0E29, 0x5E1B29, 0x5F00E9,
    0x600365, 0x6116C6, 0x620786, 0x630608, 0x640788, 0x650606, 0x660046, 0x6718A8,
    0x6858A6, 0x690145, 0x6A01E9, 0x6B178A, 0x6C01E8, 0x6D1785, 0x6E1E28, 0x6F0C65,
    0x700CC5, 0x711D5C, 0x720648, 0x730E28, 0x740646, 0x750E26, 0x761B28, 0x7700E6,
    0x7801E5, 0x791786, 0x7A1E29, 0x7B0C68, 0x7C1E24, 0x7D0C69, 0x7E0955, 0x7F03C9,
    0x8003E9, 0x810975, 0x820C49, 0x831E04, 0x840C48, 0x851E05, 0x8617A6, 0x8701C5,
    0x8800C6, 0x891B08, 0x8A0E06, 0x8B0666, 0x8C0E08, 0x8D0668, 0x8E1D7C, 0x8F0CE5,
    0x900C45, 0x911E08, 0x9217A9, 0x9301C4, 0x9417AA, 0x9501C9, 0x960169, 0x97588A,
    0x981888, 0x990066, 0x9A0709, 0x9B07A8, 0x9C0704, 0x9D07A6, 0x9E16E6, 0x9F0345,
    0xA000C9, 0xA11B05, 0xA20E09, 0xA30669, 0xA41885, 0xA50065, 0xA60706, 0xA707A5,
    0xA803A9, 0xA90189, 0xAA0029, 0xAB0889, 0xAC0744, 0xAD06E9, 0xAE0B06, 0xAF0229,
    0xB00E05, 0xB10665, 0xB21974, 0xB30CE8, 0xB4070A, 0xB507A9, 0xB616E9, 0xB70348,
    0xB8074A, 0xB906E6, 0xBA0B09, 0xBB0226, 0xBC1CE4, 0xBD0D7D, 0xBE0269, 0xBF08C9,
    0xC000CA, 0xC11B04, 0xC21884, 0xC3006A, 0xC40E04, 0xC50664, 0xC60708, 0xC707AA,
    0xC803A8, 0xC90184, 0xCA0749, 0xCB06E4, 0xCC0020, 0xCD0888, 0xCE0B08, 0xCF0224,
    0xD00E0A, 0xD1066A, 0xD20705, 0xD307A4, 0xD41D78, 0xD50CE9, 0xD616EA, 0xD70349,
    0xD80745, 0xD906E8, 0xDA1CE9, 0xDB0D75, 0xDC0B04, 0xDD0228, 0xDE0268, 0xDF08C8,
    0xE003A5, 0xE10185, 0xE20746, 0xE306EA, 0xE40748, 0xE506E5, 0xE61CE8, 0xE70D79,
    0xE81D74, 0xE95CE6, 0xEA02E9, 0xEB0849, 0xEC02E8, 0xED0848, 0xEE0086, 0xEF0A08,
    0xF00021, 0xF10885, 0xF20B05, 0xF3022A, 0xF40B0A, 0xF50225, 0xF60265, 0xF708C5,
    0xF802E5, 0xF90845, 0xFA0089, 0xFB0A09, 0xFC008A, 0xFD0A0A, 0xFE02A9, 0xFF0062
};

static inline BOOL rop_uses_src(DWORD rop)
{
    return (((rop & 0xcc0000) >> 2) != (rop & 0x330000));
}

static const char *sha1_graphics_a8r8g8b8[] =
{
    "a3cadd34d95d3d5cc23344f69aab1c2e55935fcf",
    "2426172d9e8fec27d9228088f382ef3c93717da9",
    "9e8f27ca952cdba01dbf25d07c34e86a7820c012",
    "664fac17803859a4015c6ae29e5538e314d5c827",
    "17b2c177bdce5e94433574a928bda5c94a8cdfa5",
    "fe6cc678fb13a3ead67839481bf22348adc69f52",
    "d51bd330cec510cdccf5394328bd8e5411901e9e",
    "df4aebf98d91f11be560dd232123b3ae327303d7",
    "f2af53dd073a09b1031d0032d28da35c82adc566",
    "eb5a963a6f7b25533ddfb8915e70865d037bd156",
    "c387917268455017aa0b28bed73aa6554044bbb3",
    "dcae44fee010dbf7a107797a503923fd8b1abe2e",
    "6c530622a025d872a642e8f950867884d7b136cb",
    "7c07d91b8f68fb31821701b3dcb96de018bf0c66",
    "b2261353decda2712b83538ab434a49ce21f3172",
    "35f731c0f6356b8f30651bb3cbe0d922c49deba5",
    "9b9874c1c1d92afa554137e191d34ea33acc322f",
    "c311dd74325e8cebfc8529a6d24a6fa4ecb7137e",
    "d7398de15b2837a58a62a701ca1b3384625afec4",
    "a78b28472bb7ff480ddedd06b9cf2daa775fa7ae",
    "5246ef357e7317b9d141a3294d300c195da76cb7",
    "e2a8eef4aeda3a0f6c950075acba38f1f9e0814d",
    "8b66f14d51ecdeea12bc993302bb9b7d3ec085a1",
    "7da9dd3d40d44d92deb9883fb7110443c2d5769a",
    NULL
};

static const char *sha1_graphics_a8b8g8r8[] =
{
    "a3cadd34d95d3d5cc23344f69aab1c2e55935fcf",
    "e0bc877697093ed440e125154e247ca9d65e933c",
    "c6d7faf5a502299f99d59eef3f7650bd63dbe108",
    "9d8c05c3ebd786e7d052418e905a80a64bf7853d",
    "3da12af0a810fd993fa3dbe23328a4fcd2b6c92a",
    "b91c8f21cc4d7994abc551feff5b6927d267a9db",
    "d49dd2c6a37e975b6dc3d201ccc217a788b30284",
    "ca6753f9eb44529cf8c67cd6abcd4ed1ef758904",
    "18c3ae944e0afb6c43c21cde093ddb22a27611e4",
    "b753ebb39d90210cc717f57b53dd439f7de6b077",
    "38c017dd1fff26b492a57e09f3ce2c4370faf225",
    "94368cea5033b435454daa56d55546310675131e",
    "bf57a6a37fb107d29ed3d45695919887abcb7902",
    "3db0f8bcca3d94920aa57be6321202b8c3c08822",
    "1f1fc165a4dae7ba118ddccb58a279bfe3876b0a",
    "8e09abb108e137c99527ab4c9bd07d95b9254bbb",
    "b0178632775d29bec2b16de7b9b8287115c40d0f",
    "ca7e859647b9498b53fdd92543ad8aea98ff46f3",
    "3369889a67d6c79a24ee15f7d14374f9995215e4",
    "473a1fd07df800c87a5d3286b642ace10c61c6af",
    "10cd25a0ed5cd8f978d7d68236f81d949b938e84",
    "4851c5b7d5bc18590e787c0c218a592ef504e738",
    "9aa506e3df33e0d5298755aa4144e10eb4b5adcf",
    "abdf003699364fe45fab7dc61e67c606d0063b40",
    NULL
};

static const char *sha1_graphics_24[] =
{
    "e993b15c9bd14fb45a15310450b7083c44e42665",
    "edbd7bab3d957fbc85e89612197cf918f5f5af20",
    "6a7efb3b6e0b49336df1bd2937ca09a11d976531",
    "236eb5ca9da70ec7cc719cd2fd291bab14000257",
    "f98023c7cd8c068f2d7a77ce3600004b90ea12d6",
    "5c4cb9cea2226fc671bb4a11f8253343ee94bb4b",
    "fd4be592483623dbc800fe28210a1f0daa71999b",
    "788b8de98c47974fa9f232a6042ae4ca546ddb7d",
    "a8772e6c44ba633fb384a7c4b50b435f1406107e",
    "883bc8f305c602edca785e21cd00f488583fb13f",
    "3bac4e80993f49dc3926e30524115fca9d7a8026",
    "91369e35be29059a0665782541db4c8b324c6bb2",
    "0fa8cf332a56bb6d7e14e85861fdd60f51d70501",
    "593d694cdcc8349b3bfc8257041dbcb27e61da45",
    "1036b91d93e31cd1d4740d0c8642e115e5a38188",
    "1898073cdb35ca4d2b21bba933ac16a0b4297317",
    "5068bff794553cf5a3145ae407c9a2984357844c",
    "413a7989969c229dee4ab1798362f32f96cf0a10",
    "0bb222e540b82720d4971e4a2fc626899af03e03",
    "adc20832d8c43f1cf372d8392535492013cd2306",
    "45649794dcbcabda487f66f7a80fc1bec79047a1",
    "b4df692ac70a5f9f303270df4641ab014c6cbf46",
    "8bc3128ba47891366fd7b02fde7ca19100e64b9f",
    "e649e00efe7fea1eb8b17f7867fe089e5270c44b",
    NULL
};

static const char *sha1_graphics_r5g5b5[] =
{
    "2a2ab8b3c019e70b788ade028b0e9e53ffc529ae",
    "847005cf7371f511bcc837251cde07b1796f6113",
    "a8f75743a930843ec14d516cd048b6e0468e5d89",
    "d094f51ce9b9daa9c1d9594ea88be2a2db651459",
    "cf3928e240c9149788e1635b115a4e5baea0dd8f",
    "a9034a905daa91757b4f63345c0e40638cd53ca8",
    "15ee915d989e49bb9bab5b834d8f355bd067cd8f",
    "99474fecf11df7b7035c35be6b8b697be9889418",
    "cbc2898717f97ebb07c0c7cc04abde936dc5b584",
    "29c896b591fdf4ddd23e5c0da1818c37e4686d94",
    "4b5b275d33c1ebfe5bdc61df2ad125e865b800fa",
    "92df731fa1f89550d9d4f7ea36c13f2e57c4b02a",
    "420e39ff3bdd04c4b6cc2c98e99cb7993c7a0de5",
    "1fabf0fdd046857b1974e31c1c1764fa9d1a762f",
    "449092689226a1172b6086ba1181d6b6d6499f26",
    "1a92a60f190d33ef06d9decb56fd3fdd33f3af03",
    "e61f5978c2e28c0c6d8f5eefe0f840c975586efc",
    "897d16f4d6a6ddad685d23ed7828d4f676539b75",
    "9d21bcfdeaf1ca5d47eb823bdefc24d7a95f4f56",
    "6daaf945a955928c5c124c880522ca4634fb2343",
    "12a288390d16e1efa99d4185301de48a4d433b14",
    "3a50ce21b3563a604b4fc9f247a30f5a981f1ba6",
    "d7d97e28ed316f6596c737eb83baa5948d86b673",
    "ecc2991277d7314f55b00e0f284ae3703aeef81e",
    NULL
};

static const char *sha1_graphics_r4g4b4[] =
{
    "2a2ab8b3c019e70b788ade028b0e9e53ffc529ae",
    "cfa0ab83ee93283ad914c3748f0532da1697af1d",
    "8bd18697d1ef27492805667a0bc956343ac08667",
    "e8501c830321584474654f90e40eaf27dc21e6a8",
    "d95ab10fcfb8447b41742e89f1ae8cd297a32fc4",
    "821177710961d2cb5f7e7dfc0e06e767b6042753",
    "667124365ffadeea1d8791bedda77a0c7b898de8",
    "c9f23e684b600dea17575b4b17175fbd9106c3a9",
    "7678876e50eae35d1eaa096aae25afaa0b864bf3",
    "fb52b0c373a5f2a60b981604b120962942d2447a",
    "5ab8dd07436681d762fa04ad7c6d71291c488924",
    "0167981d9e1490a2ebd396ff7582f0943aa8e1b8",
    "115a6bd382410a4a1d3c1fa71d8bf02536863e38",
    "65c6d1228e3b6d63d42647f14217bc1658b70d9a",
    "25fcb75aa687aac35b8f72640889fe92413e00c5",
    "3bddf9d53e89560b083302b146cd33791b13d941",
    "a81504498c7a7bb46340ce74476a42f70f2730b1",
    "e61a4f2657a444d8c49f84fb944f9f847667bf2b",
    "32b6e0aa79b7e96cd0ab2da167f6463c011023a8",
    "1d283aa4d2b2114f7809fe59357d590c7c779aa7",
    "29640e2ddd2d3016da14507c3ce9b2ce32f39bb4",
    "d591232bbc2592462c819a9486750f64180518fd",
    "0e183a4c30b3da345129cffe33fe0fc593d8666b",
    "f14d9a4bd8a365b7c8f068a0dad481b6eb2b178b",
    NULL
};

static const char *sha1_graphics_8[] =
{
    "41728d7ff2bb425b5fc06521adeabf6cc73136f3",
    "512246d4886ab889a090b167ba194577cb95272e",
    "921e852d4564cb9e5ac15ff68b5207bebea871d1",
    "9636b0ebefc443ea11949ccd28f6ca454277dd41",
    "aa9050da55e6b6957c60b7d603fce539cb5c0048",
    "e2b93aca15fb1233ac09a713dced1f4cd950b1e4",
    "3e3a603fc26cc305aa27f88da7d2a3b0073877d8",
    "390b2bf70daba36310683f46af9cd50b9a061396",
    "82d21737e9a7247397a6c983a9b6d9a0452dd74d",
    "2a8460af91675e01cbe9384eb6cd3eb2cb420960",
    "1af53b1218ee9844fcda891b836d42f6b2f66bd5",
    "da1cc34a9d9b779fc7849e03e214096026506464",
    "5ba8f99ca034666effa556748c49a0f5a015125f",
    "b67ba2f55659c75ac72c1112494461bb3086e1a4",
    "73e2859ce849f756f954718ce3c90f02e31712b6",
    "b1dff0f5dd233b44ee568878c5d3f8ae1d80c6d9",
    "1f27dc1a1316fb7a4a78fe40fcd4bdae3aaad218",
    "6e375e1485a1e45ac6ab10af49645d5fb2e76dff",
    "cfc67c325c7cdf96d90af9b3cceb8d0504cbb3b0",
    "7262364067e03c7fa498af1d59d228d6c63b460e",
    "5241241a355a667ef0834049adf4218e8b3f16b8",
    "1f13ea0034db4b0ffa4ddcff9664fd892058f9cd",
    "3caf512cfddfd463d0750cfe3cadb58548eb2ae8",
    "4e5e7d5fd64818b2b3d3e793c88f603b699d2f0f",
    NULL
};

static const char *sha1_graphics_4[] =
{
    "fa867e2976a549ecd3b1fa67df54963232fcef8c",
    "256d742b4da96b373b4fa5663d0ad3b5faab5c8e",
    "d96d8f4232b930bccd53b903b5efaf8c0bdb16f4",
    "9401799e6116c35e5f0e5bdca07ea25316757a72",
    "482ae2b0ef1d64752b5ef11cc7f35a33eb55d07c",
    "dcfb3e01100b41c0f75a1c5f84b6de6b90138281",
    "2505598845fa026ea7187582461efbf06cb6904f",
    "3981a19363beca8f28d32a5928ac296fd22a5296",
    "01404024ebb2c266d17d734059524d874491650f",
    "c87bbff3f83b8ec11bb03cfa9bc9ee5166c4c7ef",
    "f35c5d62853be78c5d39fb2f45200dc262aa8e18",
    "46e94a55f5f58a6b915078d8ffdc725f53aab516",
    "665bbbc749a5ffeedc0d62aef0661a5ce845b017",
    "1f26a01730f67d40ea711a50d9d801bac15a642e",
    "3b53d24178cfacba53103a44dfd5d072b15a6781",
    "c52cfd57f26037723d37192722fc3a217f280c9e",
    "e34da6500cf2e424d980714d92737cf6c31a7bda",
    "d17f4358ae529f920960ed89e535902ee13b0033",
    "0f44e12ecd1ea7e39433890443626d4fc35204a4",
    "eb38683e812fd13dca971ba8f4cfd2b6820d3524",
    "73bbc83f88f1aaa6df0158b63e70bb3165163163",
    "39c16648cf6c261be71a33cec41867f28e119b94",
    "26ad5116562e7b58c76a26eaf521e2e40899e944",
    "1bcc54eaf8e3c2b7c59ecccb23c240181d7ba8b8",
    NULL
};

static inline DWORD get_stride(BITMAPINFO *bmi)
{
    return ((bmi->bmiHeader.biBitCount * bmi->bmiHeader.biWidth + 31) >> 3) & ~3;
}

static inline DWORD get_dib_size(BITMAPINFO *bmi)
{
    return get_stride(bmi) * abs(bmi->bmiHeader.biHeight);
}

static char *hash_dib(BITMAPINFO *bmi, void *bits)
{
    DWORD dib_size = get_dib_size(bmi);
    HCRYPTHASH hash;
    char *buf;
    BYTE hash_buf[20];
    DWORD hash_size = sizeof(hash_buf);
    int i;
    static const char *hex = "0123456789abcdef";

    if(!crypt_prov) return NULL;

    if(!CryptCreateHash(crypt_prov, CALG_SHA1, 0, 0, &hash)) return NULL;

    CryptHashData(hash, bits, dib_size, 0);

    CryptGetHashParam(hash, HP_HASHVAL, NULL, &hash_size, 0);
    if(hash_size != sizeof(hash_buf)) return NULL;

    CryptGetHashParam(hash, HP_HASHVAL, hash_buf, &hash_size, 0);
    CryptDestroyHash(hash);

    buf = HeapAlloc(GetProcessHeap(), 0, hash_size * 2 + 1);

    for(i = 0; i < hash_size; i++)
    {
        buf[i * 2] = hex[hash_buf[i] >> 4];
        buf[i * 2 + 1] = hex[hash_buf[i] & 0xf];
    }
    buf[i * 2] = '\0';

    return buf;
}

static void compare_hash(BITMAPINFO *bmi, BYTE *bits, const char ***sha1, const char *info)
{
    char *hash = hash_dib(bmi, bits);

    if(!hash)
    {
        skip("SHA1 hashing unavailable on this platform\n");
        return;
    }

    if(**sha1)
    {
        ok(!strcmp(hash, **sha1), "%d: %s: expected hash %s got %s\n",
           bmi->bmiHeader.biBitCount, info, **sha1, hash);
        (*sha1)++;
    }
    else ok(**sha1 != NULL, "missing hash, got \"%s\",\n", hash);

    HeapFree(GetProcessHeap(), 0, hash);
}

static const RECT bias_check[] =
{
    {100, 100, 200, 150},
    {100, 100, 150, 200},
    {100, 100,  50, 200},
    {100, 100,   0, 150},
    {100, 100,   0,  50},
    {100, 100,  50,   0},
    {100, 100, 150,   0},
    {100, 100, 200,  50}
};

static const RECT hline_clips[] =
{
    {120, 120, 140, 120}, /* unclipped */
    {100, 122, 140, 122}, /* l edgecase */
    { 99, 124, 140, 124}, /* l edgecase clipped */
    {120, 126, 200, 126}, /* r edgecase */
    {120, 128, 201, 128}, /* r edgecase clipped */
    { 99, 130, 201, 130}, /* l and r clipped */
    {120, 100, 140, 100}, /* t edgecase */
    {120,  99, 140,  99}, /* t edgecase clipped */
    {120, 199, 140, 199}, /* b edgecase */
    {120, 200, 140, 200}, /* b edgecase clipped */
    {120, 132, 310, 132}, /* inside two clip rects */
    { 10, 134, 101, 134}, /* r end on l edgecase */
    { 10, 136, 100, 136}, /* r end on l edgecase clipped */
    {199, 138, 220, 138}, /* l end on r edgecase */
    {200, 140, 220, 140}  /* l end on r edgecase clipped */
};

static const RECT vline_clips[] =
{
    {120, 120, 120, 140}, /* unclipped */
    {100, 120, 100, 140}, /* l edgecase */
    { 99, 120,  99, 140}, /* l edgecase clipped */
    {199, 120, 199, 140}, /* r edgecase */
    {200, 120, 200, 140}, /* r edgecase clipped */
    {122,  99, 122, 201}, /* t and b clipped */
    {124, 100, 124, 140}, /* t edgecase */
    {126,  99, 126, 140}, /* t edgecase clipped */
    {128, 120, 128, 200}, /* b edgecase */
    {130, 120, 130, 201}, /* b edgecase clipped */
    {132,  12, 132, 140}, /* inside two clip rects */
    {134,  90, 134, 101}, /* b end on t edgecase */
    {136,  90, 136, 100}, /* b end on t edgecase clipped */
    {138, 199, 138, 220}, /* t end on b edgecase */
    {140, 200, 140, 220}  /* t end on b edgecase clipped */
};

static const RECT line_clips[] =
{
    { 90, 110, 310, 120},
    { 90, 120, 295, 130},
    { 90, 190, 110, 240}, /* totally clipped, moving outcodes */
    { 90, 130, 100, 135}, /* totally clipped, end pt on l edge */
    { 90, 132, 101, 137}, /* end pt just inside l edge */
    {200, 140, 210, 141}, /* totally clipped, start pt on r edge */
    {199, 142, 210, 143}  /* start pt just inside r edge */
};

static const RECT patblt_clips[] =
{
    {120, 120, 140, 126}, /* unclipped */
    {100, 130, 140, 136}, /* l edgecase */
    { 99, 140, 140, 146}, /* l edgecase clipped */
    {180, 130, 200, 136}, /* r edgecase */
    {180, 140, 201, 146}, /* r edgecase clipped */
    {120, 100, 130, 110}, /* t edgecase */
    {140,  99, 150, 110}, /* t edgecase clipped */
    {120, 180, 130, 200}, /* b edgecase */
    {140, 180, 150, 201}, /* b edgecase */
    {199, 150, 210, 156}, /* l edge on r edgecase */
    {200, 160, 210, 166}, /* l edge on r edgecase clipped */
    { 90, 150, 101, 156}, /* r edge on l edgecase */
    { 90, 160, 100, 166}, /* r edge on l edgecase clipped */
    {160,  90, 166, 101}, /* b edge on t edgecase */
    {170,  90, 176, 101}, /* b edge on t edgecase clipped */
    {160, 199, 166, 210}, /* t edge on b edgecase */
    {170, 200, 176, 210}, /* t edge on b edgecase clipped */
};

static const RECT rectangles[] =
{
    {10,   11, 100, 101},
    {250, 100, 350,  10},
    {120,  10, 120,  20}, /* zero width */
    {120,  10, 130,  10}, /* zero height */
    {120,  40, 121,  41}, /* 1 x 1 */
    {130,  50, 132,  52}, /* 2 x 2 */
    {140,  60, 143,  63}, /* 3 x 3 */
    {150,  70, 154,  74}, /* 4 x 4 */
    {120,  20, 121,  30}, /* width == 1 */
    {130,  20, 132,  30}, /* width == 2 */
    {140,  20, 143,  30}, /* width == 3 */
    {200,  20, 210,  21}, /* height == 1 */
    {200,  30, 210,  32}, /* height == 2 */
    {200,  40, 210,  43}  /* height == 3 */
};

static const BITMAPINFOHEADER dib_brush_header_8888 = {sizeof(BITMAPINFOHEADER), 16, -16, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
static const BITMAPINFOHEADER dib_brush_header_24   = {sizeof(BITMAPINFOHEADER), 16, -16, 1, 24, BI_RGB, 0, 0, 0, 0, 0};
static const BITMAPINFOHEADER dib_brush_header_555  = {sizeof(BITMAPINFOHEADER), 16, -16, 1, 16, BI_RGB, 0, 0, 0, 0, 0};
static const BITMAPINFOHEADER dib_brush_header_8    = {sizeof(BITMAPINFOHEADER), 16, -16, 1,  8, BI_RGB, 0, 0, 0, 0, 0};
static const BITMAPINFOHEADER dib_brush_header_4    = {sizeof(BITMAPINFOHEADER), 16, -16, 1,  4, BI_RGB, 0, 0, 0, 0, 0};

static void draw_graphics(HDC hdc, BITMAPINFO *bmi, BYTE *bits, const char ***sha1)
{
    DWORD dib_size = get_dib_size(bmi);
    HPEN solid_pen, dashed_pen, orig_pen;
    HBRUSH solid_brush, dib_brush, orig_brush;
    INT i, y;
    HRGN hrgn, hrgn2;
    BYTE dib_brush_buf[sizeof(BITMAPINFO) + 256 * sizeof(RGBQUAD) + 16 * 16 * sizeof(DWORD)]; /* Enough for 16 x 16 at 32 bpp */
    BITMAPINFO *brush_bi = (BITMAPINFO*)dib_brush_buf;
    BYTE *brush_bits;

    memset(bits, 0xcc, dib_size);
    compare_hash(bmi, bits, sha1, "empty");

    solid_pen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0xff));
    orig_pen = SelectObject(hdc, solid_pen);
    SetBrushOrgEx(hdc, 0, 0, NULL);

    /* horizontal and vertical lines */
    for(i = 1; i <= 16; i++)
    {
        SetROP2(hdc, i);
        MoveToEx(hdc, 10, i * 3, NULL);
        LineTo(hdc, 100, i * 3); /* l -> r */
        MoveToEx(hdc, 100, 50 + i * 3, NULL);
        LineTo(hdc, 10, 50 + i * 3); /* r -> l */
        MoveToEx(hdc, 120 + i * 3, 10, NULL);
        LineTo(hdc, 120 + i * 3, 100); /* t -> b */
        MoveToEx(hdc, 170 + i * 3, 100, NULL);
        LineTo(hdc, 170 + i * 3, 10); /* b -> t */
    }
    compare_hash(bmi, bits, sha1, "h and v solid lines");
    memset(bits, 0xcc, dib_size);

    /* diagonal lines */
    SetROP2(hdc, R2_COPYPEN);
    for(i = 0; i < 16; i++)
    {
        double s = sin(M_PI * i / 8.0);
        double c = cos(M_PI * i / 8.0);

        MoveToEx(hdc, 200.5 + 10 * c, 200.5 + 10 * s, NULL);
        LineTo(hdc, 200.5 + 100 * c, 200.5 + 100 * s);
    }
    compare_hash(bmi, bits, sha1, "diagonal solid lines");
    memset(bits, 0xcc, dib_size);

    for(i = 0; i < sizeof(bias_check) / sizeof(bias_check[0]); i++)
    {
        MoveToEx(hdc, bias_check[i].left, bias_check[i].top, NULL);
        LineTo(hdc, bias_check[i].right, bias_check[i].bottom);
    }
    compare_hash(bmi, bits, sha1, "more diagonal solid lines");
    memset(bits, 0xcc, dib_size);

    /* solid brush PatBlt */
    solid_brush = CreateSolidBrush(RGB(0x33, 0xaa, 0xff));
    orig_brush = SelectObject(hdc, solid_brush);

    for(i = 0, y = 10; i < 256; i++)
    {
        BOOL ret;

        ret = PatBlt(hdc, 10, y, 100, 10, rop3[i]);

        if(rop_uses_src(rop3[i]))
            ok(ret == FALSE, "got TRUE for %x\n", rop3[i]);
        else
        {
            ok(ret, "got FALSE for %x\n", rop3[i]);
            y += 20;
        }

    }
    compare_hash(bmi, bits, sha1, "solid patblt");
    memset(bits, 0xcc, dib_size);

    /* clipped lines */
    hrgn = CreateRectRgn(10, 10, 200, 20);
    hrgn2 = CreateRectRgn(100, 100, 200, 200);
    CombineRgn(hrgn, hrgn, hrgn2, RGN_OR);
    SetRectRgn(hrgn2, 290, 100, 300, 200);
    CombineRgn(hrgn, hrgn, hrgn2, RGN_OR);
    ExtSelectClipRgn(hdc, hrgn, RGN_COPY);
    DeleteObject(hrgn2);

    for(i = 0; i < sizeof(hline_clips)/sizeof(hline_clips[0]); i++)
    {
        MoveToEx(hdc, hline_clips[i].left, hline_clips[i].top, NULL);
        LineTo(hdc, hline_clips[i].right, hline_clips[i].bottom);
    }
    compare_hash(bmi, bits, sha1, "clipped solid hlines");
    memset(bits, 0xcc, dib_size);

    for(i = 0; i < sizeof(vline_clips)/sizeof(vline_clips[0]); i++)
    {
        MoveToEx(hdc, vline_clips[i].left, vline_clips[i].top, NULL);
        LineTo(hdc, vline_clips[i].right, vline_clips[i].bottom);
    }
    compare_hash(bmi, bits, sha1, "clipped solid vlines");
    memset(bits, 0xcc, dib_size);

    for(i = 0; i < sizeof(line_clips)/sizeof(line_clips[0]); i++)
    {
        MoveToEx(hdc, line_clips[i].left, line_clips[i].top, NULL);
        LineTo(hdc, line_clips[i].right, line_clips[i].bottom);
    }
    compare_hash(bmi, bits, sha1, "clipped solid diagonal lines");
    memset(bits, 0xcc, dib_size);

    /* clipped PatBlt */
    for(i = 0; i < sizeof(patblt_clips) / sizeof(patblt_clips[0]); i++)
    {
        PatBlt(hdc, patblt_clips[i].left, patblt_clips[i].top,
               patblt_clips[i].right - patblt_clips[i].left,
               patblt_clips[i].bottom - patblt_clips[i].top, PATCOPY);
    }
    compare_hash(bmi, bits, sha1, "clipped patblt");
    memset(bits, 0xcc, dib_size);

    /* clipped dashed lines */
    dashed_pen = CreatePen(PS_DASH, 1, RGB(0xff, 0, 0));
    SelectObject(hdc, dashed_pen);
    SetBkMode(hdc, TRANSPARENT);
    SetBkColor(hdc, RGB(0, 0xff, 0));

    for(i = 0; i < sizeof(hline_clips)/sizeof(hline_clips[0]); i++)
    {
        MoveToEx(hdc, hline_clips[i].left, hline_clips[i].top, NULL);
        LineTo(hdc, hline_clips[i].right, hline_clips[i].bottom);
    }
    compare_hash(bmi, bits, sha1, "clipped dashed hlines");
    memset(bits, 0xcc, dib_size);

    for(i = 0; i < sizeof(hline_clips)/sizeof(hline_clips[0]); i++)
    {
        MoveToEx(hdc, hline_clips[i].right - 1, hline_clips[i].bottom, NULL);
        LineTo(hdc, hline_clips[i].left - 1, hline_clips[i].top);
    }
    compare_hash(bmi, bits, sha1, "clipped dashed hlines r -> l");
    memset(bits, 0xcc, dib_size);

    for(i = 0; i < sizeof(vline_clips)/sizeof(vline_clips[0]); i++)
    {
        MoveToEx(hdc, vline_clips[i].left, vline_clips[i].top, NULL);
        LineTo(hdc, vline_clips[i].right, vline_clips[i].bottom);
    }
    compare_hash(bmi, bits, sha1, "clipped dashed vlines");
    memset(bits, 0xcc, dib_size);

    for(i = 0; i < sizeof(vline_clips)/sizeof(vline_clips[0]); i++)
    {
        MoveToEx(hdc, vline_clips[i].right, vline_clips[i].bottom - 1, NULL);
        LineTo(hdc, vline_clips[i].left, vline_clips[i].top - 1);
    }
    compare_hash(bmi, bits, sha1, "clipped dashed vlines b -> t");
    memset(bits, 0xcc, dib_size);

    for(i = 0; i < sizeof(line_clips)/sizeof(line_clips[0]); i++)
    {
        MoveToEx(hdc, line_clips[i].left, line_clips[i].top, NULL);
        LineTo(hdc, line_clips[i].right, line_clips[i].bottom);
    }
    compare_hash(bmi, bits, sha1, "clipped dashed diagonal lines");
    memset(bits, 0xcc, dib_size);

    SetBkMode(hdc, OPAQUE);

    for(i = 0; i < sizeof(line_clips)/sizeof(line_clips[0]); i++)
    {
        MoveToEx(hdc, line_clips[i].left, line_clips[i].top, NULL);
        LineTo(hdc, line_clips[i].right, line_clips[i].bottom);
    }
    compare_hash(bmi, bits, sha1, "clipped opaque dashed diagonal lines");
    memset(bits, 0xcc, dib_size);

    ExtSelectClipRgn(hdc, NULL, RGN_COPY);

    /* 8888 DIB pattern brush */

    brush_bi->bmiHeader = dib_brush_header_8888;
    brush_bits = (BYTE*)brush_bi + sizeof(BITMAPINFOHEADER);
    memset(brush_bits, 0, 16 * 16 * sizeof(DWORD));
    brush_bits[2] = 0xff;
    brush_bits[6] = 0xff;
    brush_bits[14] = 0xff;
    brush_bits[65] = 0xff;
    brush_bits[69] = 0xff;
    brush_bits[72] = 0xff;

    dib_brush = CreateDIBPatternBrushPt(brush_bi, DIB_RGB_COLORS);

    SelectObject(hdc, dib_brush);
    SetBrushOrgEx(hdc, 1, 1, NULL);

    for(i = 0, y = 10; i < 256; i++)
    {
        BOOL ret;

        if(!rop_uses_src(rop3[i]))
        {
            ret = PatBlt(hdc, 10 + i, y, 100, 20, rop3[i]);
            ok(ret, "got FALSE for %x\n", rop3[i]);
            y += 25;
        }
    }
    compare_hash(bmi, bits, sha1, "top-down 8888 dib brush patblt");
    memset(bits, 0xcc, dib_size);

    SelectObject(hdc, orig_brush);
    DeleteObject(dib_brush);

    /* 8888 bottom-up DIB pattern brush */

    brush_bi->bmiHeader.biHeight = -brush_bi->bmiHeader.biHeight;

    dib_brush = CreateDIBPatternBrushPt(brush_bi, DIB_RGB_COLORS);

    SelectObject(hdc, dib_brush);

    /* This used to set the x origin to 100 as well, but
       there's a Windows bug for 24 bpp where the brush's x offset
       is incorrectly calculated for rops that involve both D and P */
    SetBrushOrgEx(hdc, 4, 100, NULL);

    for(i = 0, y = 10; i < 256; i++)
    {
        BOOL ret;

        if(!rop_uses_src(rop3[i]))
        {
            ret = PatBlt(hdc, 10 + i, y, 100, 20, rop3[i]);
            ok(ret, "got FALSE for %x\n", rop3[i]);
            y += 25;
        }
    }
    compare_hash(bmi, bits, sha1, "bottom-up 8888 dib brush patblt");
    memset(bits, 0xcc, dib_size);

    /* 24 bpp dib pattern brush */

    brush_bi->bmiHeader = dib_brush_header_24;
    brush_bits = (BYTE*)brush_bi + sizeof(BITMAPINFOHEADER);
    memset(brush_bits, 0, 16 * 16 * 3);
    brush_bits[0] = brush_bits[3] = brush_bits[6] = brush_bits[8] = 0xff;
    brush_bits[49] = brush_bits[52] = 0xff;

    dib_brush = CreateDIBPatternBrushPt(brush_bi, DIB_RGB_COLORS);

    SelectObject(hdc, dib_brush);
    SetBrushOrgEx(hdc, 1, 1, NULL);

    for(i = 0, y = 10; i < 256; i++)
    {
        BOOL ret;

        if(!rop_uses_src(rop3[i]))
        {
            ret = PatBlt(hdc, 10 + i, y, 100, 20, rop3[i]);
            ok(ret, "got FALSE for %x\n", rop3[i]);
            y += 25;
        }
    }
    compare_hash(bmi, bits, sha1, "top-down 24 bpp brush patblt");
    memset(bits, 0xcc, dib_size);

    SelectObject(hdc, orig_brush);
    DeleteObject(dib_brush);

    /* 555 dib pattern brush */

    brush_bi->bmiHeader = dib_brush_header_555;
    brush_bits = (BYTE*)brush_bi + sizeof(BITMAPINFOHEADER);
    memset(brush_bits, 0, 16 * 16 * sizeof(WORD));
    brush_bits[0] = brush_bits[1] = 0xff;
    brush_bits[32] = brush_bits[34] = 0x7c;

    dib_brush = CreateDIBPatternBrushPt(brush_bi, DIB_RGB_COLORS);

    SelectObject(hdc, dib_brush);
    SetBrushOrgEx(hdc, 1, 1, NULL);

    for(i = 0, y = 10; i < 256; i++)
    {
        BOOL ret;

        if(!rop_uses_src(rop3[i]))
        {
            ret = PatBlt(hdc, 10 + i, y, 100, 20, rop3[i]);
            ok(ret, "got FALSE for %x\n", rop3[i]);
            y += 25;
        }
    }
    compare_hash(bmi, bits, sha1, "top-down 555 dib brush patblt");
    memset(bits, 0xcc, dib_size);

    SelectObject(hdc, orig_brush);
    DeleteObject(dib_brush);

    SetBrushOrgEx(hdc, 0, 0, NULL);

    /* 8 bpp dib pattern brush */

    brush_bi->bmiHeader = dib_brush_header_8;
    brush_bi->bmiHeader.biClrUsed = 3;
    memset(brush_bi->bmiColors, 0, brush_bi->bmiHeader.biClrUsed * sizeof(RGBQUAD));
    brush_bi->bmiColors[0].rgbRed = 0xff;
    brush_bi->bmiColors[1].rgbRed = 0xff;
    brush_bi->bmiColors[1].rgbGreen = 0xff;
    brush_bi->bmiColors[1].rgbBlue = 0xff;

    brush_bits = (BYTE*)brush_bi + sizeof(BITMAPINFOHEADER) + brush_bi->bmiHeader.biClrUsed * sizeof(RGBQUAD);
    memset(brush_bits, 0, 16 * 16 * sizeof(BYTE));
    brush_bits[0] = brush_bits[1] = 1;
    brush_bits[16] = brush_bits[17] = 2;
    brush_bits[32] = brush_bits[33] = 6;

    dib_brush = CreateDIBPatternBrushPt(brush_bi, DIB_RGB_COLORS);

    SelectObject(hdc, dib_brush);
    SetBrushOrgEx(hdc, 1, 1, NULL);

    for(i = 0, y = 10; i < 256; i++)
    {
        BOOL ret;

        if(!rop_uses_src(rop3[i]))
        {
            ret = PatBlt(hdc, 10 + i, y, 100, 20, rop3[i]);
            ok(ret, "got FALSE for %x\n", rop3[i]);
            y += 25;
        }
    }
    compare_hash(bmi, bits, sha1, "top-down 8 bpp dib brush patblt");
    memset(bits, 0xcc, dib_size);

    SelectObject(hdc, orig_brush);
    DeleteObject(dib_brush);

    /* 4 bpp dib pattern brush */

    brush_bi->bmiHeader = dib_brush_header_4;
    dib_brush = CreateDIBPatternBrushPt(brush_bi, DIB_RGB_COLORS);

    SelectObject(hdc, dib_brush);
    SetBrushOrgEx(hdc, 1, 1, NULL);

    for(i = 0, y = 10; i < 256; i++)
    {
        BOOL ret;

        if(!rop_uses_src(rop3[i]))
        {
            ret = PatBlt(hdc, 10 + i, y, 100, 20, rop3[i]);
            ok(ret, "got FALSE for %x\n", rop3[i]);
            y += 25;
        }
    }
    compare_hash(bmi, bits, sha1, "top-down 4 bpp dib brush patblt");
    memset(bits, 0xcc, dib_size);

    SelectObject(hdc, orig_brush);
    SetBrushOrgEx(hdc, 0, 0, NULL);

    /* Rectangle */

    SelectObject(hdc, solid_pen);
    SelectObject(hdc, solid_brush);

    for(i = 0; i < sizeof(rectangles)/sizeof(rectangles[0]); i++)
    {
        Rectangle(hdc, rectangles[i].left, rectangles[i].top, rectangles[i].right, rectangles[i].bottom);
    }

    SelectObject(hdc, dashed_pen);
    for(i = 0; i < sizeof(rectangles)/sizeof(rectangles[0]); i++)
    {
        Rectangle(hdc, rectangles[i].left, rectangles[i].top + 150, rectangles[i].right, rectangles[i].bottom + 150);
    }

    compare_hash(bmi, bits, sha1, "rectangles");
    memset(bits, 0xcc, dib_size);
    SelectObject(hdc, solid_pen);

    /* PaintRgn */

    PaintRgn(hdc, hrgn);
    compare_hash(bmi, bits, sha1, "PaintRgn");
    memset(bits, 0xcc, dib_size);

    /* RTL rectangles */

    if( !pSetLayout )
    {
        win_skip("Don't have SetLayout\n");
        (*sha1)++;
    }
    else
    {
        pSetLayout(hdc, LAYOUT_RTL);
        PaintRgn(hdc, hrgn);
        PatBlt(hdc, 10, 250, 10, 10, PATCOPY);
        Rectangle(hdc, 100, 250, 110, 260);
        compare_hash(bmi, bits, sha1, "rtl");
        memset(bits, 0xcc, dib_size);

        pSetLayout(hdc, LAYOUT_LTR);
    }

    SelectObject(hdc, orig_brush);
    SelectObject(hdc, orig_pen);
    DeleteObject(hrgn);
    DeleteObject(dib_brush);
    DeleteObject(dashed_pen);
    DeleteObject(solid_brush);
    DeleteObject(solid_pen);
}

static void test_simple_graphics(void)
{
    char bmibuf[sizeof(BITMAPINFO) + 256 * sizeof(RGBQUAD)];
    BITMAPINFO *bmi = (BITMAPINFO *)bmibuf;
    DWORD *bit_fields = (DWORD*)(bmibuf + sizeof(BITMAPINFOHEADER));
    HDC mem_dc;
    BYTE *bits;
    HBITMAP dib, orig_bm;
    const char **sha1;
    DIBSECTION ds;

    mem_dc = CreateCompatibleDC(NULL);

    /* a8r8g8b8 */
    trace("8888\n");
    memset(bmi, 0, sizeof(bmibuf));
    bmi->bmiHeader.biSize = sizeof(bmi->bmiHeader);
    bmi->bmiHeader.biHeight = 512;
    bmi->bmiHeader.biWidth = 512;
    bmi->bmiHeader.biBitCount = 32;
    bmi->bmiHeader.biPlanes = 1;
    bmi->bmiHeader.biCompression = BI_RGB;

    dib = CreateDIBSection(0, bmi, DIB_RGB_COLORS, (void**)&bits, NULL, 0);
    ok(dib != NULL, "ret NULL\n");
    ok(GetObjectW( dib, sizeof(ds), &ds ), "GetObject failed\n");
    ok(ds.dsBitfields[0] == 0, "got %08x\n", ds.dsBitfields[0]);
    ok(ds.dsBitfields[1] == 0, "got %08x\n", ds.dsBitfields[1]);
    ok(ds.dsBitfields[2] == 0, "got %08x\n", ds.dsBitfields[2]);
    ok(ds.dsBmih.biCompression == BI_RGB ||
       broken(ds.dsBmih.biCompression == BI_BITFIELDS), /* nt4 sp1 and 2 */
       "got %x\n", ds.dsBmih.biCompression);

    orig_bm = SelectObject(mem_dc, dib);

    sha1 = sha1_graphics_a8r8g8b8;
    draw_graphics(mem_dc, bmi, bits, &sha1);

    SelectObject(mem_dc, orig_bm);
    DeleteObject(dib);

    /* a8r8g8b8 - bitfields.  Should be the same as the regular 32 bit case.*/
    trace("8888 - bitfields\n");
    bmi->bmiHeader.biBitCount = 32;
    bmi->bmiHeader.biCompression = BI_BITFIELDS;
    bit_fields[0] = 0xff0000;
    bit_fields[1] = 0x00ff00;
    bit_fields[2] = 0x0000ff;

    dib = CreateDIBSection(mem_dc, bmi, DIB_RGB_COLORS, (void**)&bits, NULL, 0);
    ok(dib != NULL, "ret NULL\n");
    ok(GetObjectW( dib, sizeof(ds), &ds ), "GetObject failed\n");
    ok(ds.dsBitfields[0] == 0xff0000, "got %08x\n", ds.dsBitfields[0]);
    ok(ds.dsBitfields[1] == 0x00ff00, "got %08x\n", ds.dsBitfields[1]);
    ok(ds.dsBitfields[2] == 0x0000ff, "got %08x\n", ds.dsBitfields[2]);
    ok(ds.dsBmih.biCompression == BI_BITFIELDS, "got %x\n", ds.dsBmih.biCompression);

    orig_bm = SelectObject(mem_dc, dib);

    sha1 = sha1_graphics_a8r8g8b8;
    draw_graphics(mem_dc, bmi, bits, &sha1);

    SelectObject(mem_dc, orig_bm);
    DeleteObject(dib);

    /* a8b8g8r8 - bitfields. */
    trace("a8b8g8r8 - bitfields\n");
    bmi->bmiHeader.biBitCount = 32;
    bmi->bmiHeader.biCompression = BI_BITFIELDS;
    bit_fields[0] = 0x0000ff;
    bit_fields[1] = 0x00ff00;
    bit_fields[2] = 0xff0000;

    dib = CreateDIBSection(mem_dc, bmi, DIB_RGB_COLORS, (void**)&bits, NULL, 0);
    ok(dib != NULL, "ret NULL\n");
    ok(GetObjectW( dib, sizeof(ds), &ds ), "GetObject failed\n");
    ok(ds.dsBitfields[0] == 0x0000ff, "got %08x\n", ds.dsBitfields[0]);
    ok(ds.dsBitfields[1] == 0x00ff00, "got %08x\n", ds.dsBitfields[1]);
    ok(ds.dsBitfields[2] == 0xff0000, "got %08x\n", ds.dsBitfields[2]);
    ok(ds.dsBmih.biCompression == BI_BITFIELDS, "got %x\n", ds.dsBmih.biCompression);

    orig_bm = SelectObject(mem_dc, dib);

    sha1 = sha1_graphics_a8b8g8r8;
    draw_graphics(mem_dc, bmi, bits, &sha1);

    SelectObject(mem_dc, orig_bm);
    DeleteObject(dib);

    /* 24 */
    trace("24\n");
    bmi->bmiHeader.biBitCount = 24;
    bmi->bmiHeader.biCompression = BI_RGB;

    dib = CreateDIBSection(0, bmi, DIB_RGB_COLORS, (void**)&bits, NULL, 0);
    ok(dib != NULL, "ret NULL\n");
    orig_bm = SelectObject(mem_dc, dib);

    sha1 = sha1_graphics_24;
    draw_graphics(mem_dc, bmi, bits, &sha1);

    SelectObject(mem_dc, orig_bm);
    DeleteObject(dib);

    /* r5g5b5 */
    trace("555\n");
    bmi->bmiHeader.biBitCount = 16;
    bmi->bmiHeader.biCompression = BI_RGB;

    dib = CreateDIBSection(0, bmi, DIB_RGB_COLORS, (void**)&bits, NULL, 0);
    ok(dib != NULL, "ret NULL\n");
    ok(GetObjectW( dib, sizeof(ds), &ds ), "GetObject failed\n");
    ok(ds.dsBitfields[0] == 0x7c00, "got %08x\n", ds.dsBitfields[0]);
    ok(ds.dsBitfields[1] == 0x03e0, "got %08x\n", ds.dsBitfields[1]);
    ok(ds.dsBitfields[2] == 0x001f, "got %08x\n", ds.dsBitfields[2]);
todo_wine
    ok(ds.dsBmih.biCompression == BI_BITFIELDS, "got %x\n", ds.dsBmih.biCompression);

    orig_bm = SelectObject(mem_dc, dib);

    sha1 = sha1_graphics_r5g5b5;
    draw_graphics(mem_dc, bmi, bits, &sha1);

    SelectObject(mem_dc, orig_bm);
    DeleteObject(dib);

    /* r4g4b4 */
    trace("444\n");
    bmi->bmiHeader.biBitCount = 16;
    bmi->bmiHeader.biCompression = BI_BITFIELDS;
    bit_fields[0] = 0x0f00;
    bit_fields[1] = 0x00f0;
    bit_fields[2] = 0x000f;
    dib = CreateDIBSection(0, bmi, DIB_RGB_COLORS, (void**)&bits, NULL, 0);
    ok(dib != NULL, "ret NULL\n");
    ok(GetObjectW( dib, sizeof(ds), &ds ), "GetObject failed\n");
    ok(ds.dsBitfields[0] == 0x0f00, "got %08x\n", ds.dsBitfields[0]);
    ok(ds.dsBitfields[1] == 0x00f0, "got %08x\n", ds.dsBitfields[1]);
    ok(ds.dsBitfields[2] == 0x000f, "got %08x\n", ds.dsBitfields[2]);
    ok(ds.dsBmih.biCompression == BI_BITFIELDS, "got %x\n", ds.dsBmih.biCompression);

    orig_bm = SelectObject(mem_dc, dib);

    sha1 = sha1_graphics_r4g4b4;
    draw_graphics(mem_dc, bmi, bits, &sha1);

    SelectObject(mem_dc, orig_bm);
    DeleteObject(dib);

    /* 8 */
    trace("8\n");
    bmi->bmiHeader.biBitCount = 8;
    bmi->bmiHeader.biCompression = BI_RGB;
    bmi->bmiHeader.biClrUsed = 5;
    bmi->bmiColors[0].rgbRed = 0xff;
    bmi->bmiColors[0].rgbGreen = 0xff;
    bmi->bmiColors[0].rgbBlue = 0xff;
    bmi->bmiColors[1].rgbRed = 0;
    bmi->bmiColors[1].rgbGreen = 0;
    bmi->bmiColors[1].rgbBlue = 0;
    bmi->bmiColors[2].rgbRed = 0xff;
    bmi->bmiColors[2].rgbGreen = 0;
    bmi->bmiColors[2].rgbBlue = 0;
    bmi->bmiColors[3].rgbRed = 0;
    bmi->bmiColors[3].rgbGreen = 0xff;
    bmi->bmiColors[3].rgbBlue = 0;
    bmi->bmiColors[4].rgbRed = 0;
    bmi->bmiColors[4].rgbGreen = 0;
    bmi->bmiColors[4].rgbBlue = 0xff;

    dib = CreateDIBSection(0, bmi, DIB_RGB_COLORS, (void**)&bits, NULL, 0);
    ok(dib != NULL, "ret NULL\n");

    orig_bm = SelectObject(mem_dc, dib);

    sha1 = sha1_graphics_8;
    draw_graphics(mem_dc, bmi, bits, &sha1);

    SelectObject(mem_dc, orig_bm);
    DeleteObject(dib);

    /* 4 */
    trace("4\n");
    bmi->bmiHeader.biBitCount = 4;

    dib = CreateDIBSection(0, bmi, DIB_RGB_COLORS, (void**)&bits, NULL, 0);
    ok(dib != NULL, "ret NULL\n");

    orig_bm = SelectObject(mem_dc, dib);

    sha1 = sha1_graphics_4;
    draw_graphics(mem_dc, bmi, bits, &sha1);

    SelectObject(mem_dc, orig_bm);
    DeleteObject(dib);

    DeleteDC(mem_dc);
}

START_TEST(dib)
{
    HMODULE mod = GetModuleHandleA("gdi32.dll");
    pSetLayout = (void *)GetProcAddress( mod, "SetLayout" );

    CryptAcquireContextW(&crypt_prov, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT);

    test_simple_graphics();

    CryptReleaseContext(crypt_prov, 0);
}
