#pragma once
#include <cstdio>
#include <cstring>

namespace driver_selection {
inline const char* name(int choice){
    switch(choice){case 2:return "Bing";case 3:return "Paradise";case 4:return "TWT";case 5:return "WIND";case 6:return "NB9 (ANOTHER)";default:return "None";}
}
inline int parse(const char* text){
    if(!text)return 0;
    while(*text==' '||*text=='\t'||*text=='\r'||*text=='\n')++text;
    if(*text<'2'||*text>'6')return 0;
    const int choice=*text++-'0';
    while(*text==' '||*text=='\t'||*text=='\r'||*text=='\n')++text;
    return *text?0:choice;
}
inline int prompt(FILE* input,FILE* output){
    std::fputs("\n2. Bing Driver\n3. Paradise Driver\n4. TWT Driver\n5. WIND Driver\n6. NB9 Driver (ANOTHER)\n",output);
    char line[64];
    while(true){
        std::fputs("Select driver (2-6): ",output);std::fflush(output);
        if(!std::fgets(line,sizeof(line),input))return 0;
        if(!std::strchr(line,'\n')&&!std::feof(input)){
            int c;while((c=std::fgetc(input))!='\n'&&c!=EOF){}
        }else if(const int choice=parse(line))return choice;
        std::fputs("Invalid driver choice; enter 2, 3, 4, 5 or 6.\n",output);
    }
}
}
