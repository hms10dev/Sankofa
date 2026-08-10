// Host test for daysFromCivil + wrap logic (extracted from FlashcardReviewActivity).
#include <cstdio>
#include <string>
#include <vector>
#include <functional>

static long daysFromCivil(int y, unsigned m, unsigned d){
  y -= (m <= 2);
  const int era = (y >= 0 ? y : y-399) / 400;
  const unsigned yoe = (unsigned)(y - era*400);
  const unsigned doy = (153u*(m + (m>2?-3:9)) + 2)/5 + d-1;
  const unsigned doe = yoe*365 + yoe/4 - yoe/100 + doy;
  return (long)era*146097 + (long)doe - 719468;
}
// wrap with injected measure(text)->px
static std::vector<std::string> wrap(std::function<int(const std::string&)> measure,
                                     const std::string& text, int maxWidth){
  std::vector<std::string> lines; std::string line, word;
  auto flush=[&]{ if(word.empty())return;
    std::string cand = line.empty()?word:line+" "+word;
    if(measure(cand)<=maxWidth || line.empty()) line=cand;
    else { lines.push_back(line); line=word; } word.clear(); };
  for(char c: text){ if(c==' '||c=='\n'){ flush(); if(c=='\n'){lines.push_back(line);line.clear();} } else word.push_back(c); }
  flush(); if(!line.empty()) lines.push_back(line); if(lines.empty()) lines.push_back(""); return lines;
}

int fail=0,pass=0;
#define EQ(g,w,m) do{long _g=(long)(g),_w=(long)(w); if(_g==_w){pass++;printf("  PASS %s (=%ld)\n",m,_g);} else {fail++;printf("  FAIL %s got %ld want %ld\n",m,_g,_w);} }while(0)

int main(){
  printf("== daysFromCivil ==\n");
  EQ(daysFromCivil(1970,1,1),0,"1970-01-01");
  EQ(daysFromCivil(1970,1,2),1,"1970-01-02");
  EQ(daysFromCivil(2000,1,1),10957,"2000-01-01");
  EQ(daysFromCivil(2024,2,29),19782,"2024-02-29 leap");
  EQ(daysFromCivil(2026,8,10),20675,"2026-08-10 (today)");
  EQ(daysFromCivil(1969,12,31),-1,"1969-12-31");

  printf("\n== wrap (measure = 10px/char, maxWidth=100 => 10 chars/line) ==\n");
  auto m=[](const std::string&s){return (int)s.size()*10;};
  auto a=wrap(m,"hola mundo feliz",100);   // "hola mundo"=10ch=100 ok; +" feliz" too wide
  EQ(a.size(),2,"wraps into 2 lines");
  printf("   line0='%s' line1='%s'\n",a[0].c_str(),a[1].c_str());
  auto b=wrap(m,"supercalifragilistic",100); // single 20ch word > width -> placed alone
  EQ(b.size(),1,"overlong single word stays one line (no infinite loop)");
  auto c=wrap(m,"a b c",100);
  EQ(c.size(),1,"short text one line");

  printf("\n%d passed, %d failed\n",pass,fail);
  return fail?1:0;
}
