#include <iostream>
#include <memory.h>
#include <string>
#include <string.h>
//#include "Vulcan/Common/EBS/Instrument.h"

using namespace std;

static const int HashSize = 256*256;
int getSymbolHash(const char* symbol, int* w) 
{
// symbol is assumed in "EUR/USD" format
	int hash = w[0]*symbol[0] + w[1]*symbol[1] +  w[2]*symbol[2] + w[3]*symbol[4] + w[4]*symbol[5] + w[5]*symbol[6] - 65*(w[0]+w[1]+w[2]+w[3]+w[4]+w[5]);
	if (__builtin_expect((hash >= HashSize),0)) {
		return HashSize-1;
	}
	return hash;
}

int main() 
{
    const char* wordlist[] = { "", "", "", "EUR/AUD", "USD/AED", "EUR/CAD", "USD/CAD", "EUR/DKK",
            "USD/ZAR", "EUR/CZK", "USD/SAR", "USD/SGD", "EUR/RUB", "USD/RUB", "EUR/SEK", "USD/SEK", "USD/KWD", "",
            "BKT/RUB", "", "GBP/AUD", "USD/KZT", "DLR/KET", "EUR/TRY", "USD/TRY", "EUR/USD", "DLR/KES", "EUR/RON",
            "USD/BHD", "EUR/JPY", "USD/JPY", "", "USD/MXN", "EUR/CNH", "USD/CNH", "SAU/USD", "EUR/CHF", "USD/CHF",
            "LPD/USD", "USD/HKD", "", "NZD/USD", "GBP/USD", "LPT/USD", "", "NZD/JPY", "GBP/JPY", "EUR/NZD", "EUR/GBP",
            "USD/KZA", "GBP/CNH", "EUR/NOK", "USD/NOK", "GBP/CHF", "CAD/JPY", "XAU/USD", "XAG/USD", "XPD/USD",
            "EUR/PLN", "USD/PLN", "USD/INR", "AUD/USD", "XPT/USD", "", "GBP/NZD", "AUD/JPY", "", "", "", "EUR/HUF",
            "USD/ILS", "", "", "", "", "", "", "", "", "", "", "", "CNH/JPY", "AUD/NZD", "CNH/MXN", "CHF/JPY", "", "",
            "", "", "", "CNH/HKD" };

    const char* workPtr[HashSize];
    int w[6];
    int minw = HashSize/256*10+1;
    for (w[0] = 0; w[0]<10; ++w[0]) 
    for (w[1] = 0; w[1]<10; ++w[1]) 
    for (w[2] = 0; w[2]<10; ++w[2]) 
    for (w[3] = 0; w[3]<10; ++w[3]) 
    for (w[4] = 0; w[4]<10; ++w[4]) 
    for (w[5] = 0; w[5]<10; ++w[5]) 
    {
    int totw = w[0]+w[1]+w[2]+w[3]+w[4]+w[5];
    if (totw >= minw) break;
    for (int i=0; i<HashSize; ++i) workPtr[i] = NULL;
    int i = 0;
    for (; i<91; ++i)
    {
        if (strlen(wordlist[i])==7)
        {
	int hash = getSymbolHash(wordlist[i],w);
        if (workPtr[hash] != NULL)
            break;
	workPtr[hash] = wordlist[i] ; 
        };
    };
    if (i==91)
    { cout << "found!" << w[0] << w[1] << w[2] << w[3] << w[4] << w[5] << endl; 
       minw = totw;
       int minHash = HashSize, maxHash = 0;
       for (int k = 0; k<91; ++k) 
       { 
           if (strlen(wordlist[k])!=7) continue; 
           int hash = getSymbolHash(wordlist[k],w); 
           cout << hash << ", "; 
           if (hash < minHash) 
                minHash = hash ;
           else if (hash > maxHash) 
                maxHash = hash;
       };
       cout << endl << "MinHash = " << minHash << ", MaxHash = " << maxHash << ", Range = " << maxHash - minHash << endl;
    };
    };
    cout << "NO" << endl;
    return 1;
};
