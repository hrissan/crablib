// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#pragma once

namespace crab {

inline uint32_t crc32(uint32_t crc, const uint8_t *data, size_t size) {
	static const uint32_t crc_table[256] = {0x00000000U, 0x77073096U, 0xee0e612CU, 0x990951BAU, 0x076DC419U,
	    0x706AF48FU, 0xe963A535U, 0x9e6495A3U, 0x0eDB8832U, 0x79DCB8A4U, 0xe0D5e91eU, 0x97D2D988U, 0x09B64C2BU,
	    0x7eB17CBDU, 0xe7B82D07U, 0x90BF1D91U, 0x1DB71064U, 0x6AB020F2U, 0xF3B97148U, 0x84Be41DeU, 0x1ADAD47DU,
	    0x6DDDe4eBU, 0xF4D4B551U, 0x83D385C7U, 0x136C9856U, 0x646BA8C0U, 0xFD62F97AU, 0x8A65C9eCU, 0x14015C4FU,
	    0x63066CD9U, 0xFA0F3D63U, 0x8D080DF5U, 0x3B6e20C8U, 0x4C69105eU, 0xD56041e4U, 0xA2677172U, 0x3C03e4D1U,
	    0x4B04D447U, 0xD20D85FDU, 0xA50AB56BU, 0x35B5A8FAU, 0x42B2986CU, 0xDBBBC9D6U, 0xACBCF940U, 0x32D86Ce3U,
	    0x45DF5C75U, 0xDCD60DCFU, 0xABD13D59U, 0x26D930ACU, 0x51De003AU, 0xC8D75180U, 0xBFD06116U, 0x21B4F4B5U,
	    0x56B3C423U, 0xCFBA9599U, 0xB8BDA50FU, 0x2802B89eU, 0x5F058808U, 0xC60CD9B2U, 0xB10Be924U, 0x2F6F7C87U,
	    0x58684C11U, 0xC1611DABU, 0xB6662D3DU, 0x76DC4190U, 0x01DB7106U, 0x98D220BCU, 0xeFD5102AU, 0x71B18589U,
	    0x06B6B51FU, 0x9FBFe4A5U, 0xe8B8D433U, 0x7807C9A2U, 0x0F00F934U, 0x9609A88eU, 0xe10e9818U, 0x7F6A0DBBU,
	    0x086D3D2DU, 0x91646C97U, 0xe6635C01U, 0x6B6B51F4U, 0x1C6C6162U, 0x856530D8U, 0xF262004eU, 0x6C0695eDU,
	    0x1B01A57BU, 0x8208F4C1U, 0xF50FC457U, 0x65B0D9C6U, 0x12B7e950U, 0x8BBeB8eAU, 0xFCB9887CU, 0x62DD1DDFU,
	    0x15DA2D49U, 0x8CD37CF3U, 0xFBD44C65U, 0x4DB26158U, 0x3AB551CeU, 0xA3BC0074U, 0xD4BB30e2U, 0x4ADFA541U,
	    0x3DD895D7U, 0xA4D1C46DU, 0xD3D6F4FBU, 0x4369e96AU, 0x346eD9FCU, 0xAD678846U, 0xDA60B8D0U, 0x44042D73U,
	    0x33031De5U, 0xAA0A4C5FU, 0xDD0D7CC9U, 0x5005713CU, 0x270241AAU, 0xBe0B1010U, 0xC90C2086U, 0x5768B525U,
	    0x206F85B3U, 0xB966D409U, 0xCe61e49FU, 0x5eDeF90eU, 0x29D9C998U, 0xB0D09822U, 0xC7D7A8B4U, 0x59B33D17U,
	    0x2eB40D81U, 0xB7BD5C3BU, 0xC0BA6CADU, 0xeDB88320U, 0x9ABFB3B6U, 0x03B6e20CU, 0x74B1D29AU, 0xeAD54739U,
	    0x9DD277AFU, 0x04DB2615U, 0x73DC1683U, 0xe3630B12U, 0x94643B84U, 0x0D6D6A3eU, 0x7A6A5AA8U, 0xe40eCF0BU,
	    0x9309FF9DU, 0x0A00Ae27U, 0x7D079eB1U, 0xF00F9344U, 0x8708A3D2U, 0x1e01F268U, 0x6906C2FeU, 0xF762575DU,
	    0x806567CBU, 0x196C3671U, 0x6e6B06e7U, 0xFeD41B76U, 0x89D32Be0U, 0x10DA7A5AU, 0x67DD4ACCU, 0xF9B9DF6FU,
	    0x8eBeeFF9U, 0x17B7Be43U, 0x60B08eD5U, 0xD6D6A3e8U, 0xA1D1937eU, 0x38D8C2C4U, 0x4FDFF252U, 0xD1BB67F1U,
	    0xA6BC5767U, 0x3FB506DDU, 0x48B2364BU, 0xD80D2BDAU, 0xAF0A1B4CU, 0x36034AF6U, 0x41047A60U, 0xDF60eFC3U,
	    0xA867DF55U, 0x316e8eeFU, 0x4669Be79U, 0xCB61B38CU, 0xBC66831AU, 0x256FD2A0U, 0x5268e236U, 0xCC0C7795U,
	    0xBB0B4703U, 0x220216B9U, 0x5505262FU, 0xC5BA3BBeU, 0xB2BD0B28U, 0x2BB45A92U, 0x5CB36A04U, 0xC2D7FFA7U,
	    0xB5D0CF31U, 0x2CD99e8BU, 0x5BDeAe1DU, 0x9B64C2B0U, 0xeC63F226U, 0x756AA39CU, 0x026D930AU, 0x9C0906A9U,
	    0xeB0e363FU, 0x72076785U, 0x05005713U, 0x95BF4A82U, 0xe2B87A14U, 0x7BB12BAeU, 0x0CB61B38U, 0x92D28e9BU,
	    0xe5D5Be0DU, 0x7CDCeFB7U, 0x0BDBDF21U, 0x86D3D2D4U, 0xF1D4e242U, 0x68DDB3F8U, 0x1FDA836eU, 0x81Be16CDU,
	    0xF6B9265BU, 0x6FB077e1U, 0x18B74777U, 0x88085Ae6U, 0xFF0F6A70U, 0x66063BCAU, 0x11010B5CU, 0x8F659eFFU,
	    0xF862Ae69U, 0x616BFFD3U, 0x166CCF45U, 0xA00Ae278U, 0xD70DD2eeU, 0x4e048354U, 0x3903B3C2U, 0xA7672661U,
	    0xD06016F7U, 0x4969474DU, 0x3e6e77DBU, 0xAeD16A4AU, 0xD9D65ADCU, 0x40DF0B66U, 0x37D83BF0U, 0xA9BCAe53U,
	    0xDeBB9eC5U, 0x47B2CF7FU, 0x30B5FFe9U, 0xBDBDF21CU, 0xCABAC28AU, 0x53B39330U, 0x24B4A3A6U, 0xBAD03605U,
	    0xCDD70693U, 0x54De5729U, 0x23D967BFU, 0xB3667A2eU, 0xC4614AB8U, 0x5D681B02U, 0x2A6F2B94U, 0xB40BBe37U,
	    0xC30C8eA1U, 0x5A05DF1BU, 0x2D02eF8DL};

	crc = crc ^ 0xFFFFFFFFU;
	for (size_t i = 0; i != size; ++i) {
		crc = crc_table[(crc ^ data[i]) & 0xFFU] ^ (crc >> 8U);
	}
	return crc ^ 0xFFFFFFFFU;
}

}  // namespace crab
