"""Host regression: real HLP protocol/core/decoder; fake UART, clock, UI and config sink.
Run: python tests/hlp_protocol_test.py --idf C:/esp/v5.5.5/esp-idf
Requires MSVC (Windows) or g++ (POSIX). No device/NVS access.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--idf', default=os.environ.get('IDF_PATH'))
args = parser.parse_args()
assert args.idf, 'Supply --idf (cJSON source dependency)'
root = Path(__file__).resolve().parents[1]
headers = {
    'esp_err.h': '#pragma once\nusing esp_err_t=int;\n#define ESP_OK 0\n#define ESP_ERR_NO_MEM 1\ninline const char *esp_err_to_name(int){return "error";}\n',
    'esp_check.h': '#define ESP_RETURN_ON_FALSE(c,e,...) if(!(c)) return e\n',
    'esp_log.h': '#define ESP_LOGW(...) ((void)0)\n#define ESP_LOGI(...) ((void)0)\n#define ESP_LOGE(...) ((void)0)\n#define ESP_LOGD(...) ((void)0)\n',
    'sdkconfig.h': '#define CONFIG_WAZE_HUD_DISPLAY_CYD_28 1\n',
    'freertos/FreeRTOS.h': '#pragma once\n#include <cstdint>\nusing TickType_t=uint32_t; using BaseType_t=int; using portMUX_TYPE=int;\n#define portMUX_INITIALIZER_UNLOCKED 0\n#define pdMS_TO_TICKS(x) (x)\n#define pdPASS 1\n',
    'freertos/queue.h': '#pragma once\nusing QueueHandle_t=void*;\n',
    'freertos/task.h': '#pragma once\n#include "freertos/FreeRTOS.h"\nextern TickType_t ticks;\ninline TickType_t xTaskGetTickCount(){return ticks;}\ninline int xTaskCreate(void(*)(void*),const char*,int,void*,int,void*){return pdPASS;}\ninline void vTaskDelete(void*){}\n',
    'esp_timer.h': '#pragma once\n#include <cstdint>\nextern uint32_t ticks;\ninline int64_t esp_timer_get_time(){return int64_t(ticks)*1000;}\n',
}
harness = r'''
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#define private public
#include "protocol/hlp_protocol.h"
#undef private
#include "protocol/hlp_protocol.cpp"
#include "serial/serial_transport.h"
#include "state/hud_state_store.h"
#include "config/device_config.h"
uint32_t ticks=1;
using namespace waze_hud;
struct Stop {};
struct Input { uint32_t at; std::string line; bool reset=false; };
std::vector<Input> inputs;
size_t nextInput=0;
std::vector<std::string> output;
HudState current;
int configHi=0;
void check(bool ok,const char *why){if(!ok) throw std::runtime_error(why);}
SerialTransport &SerialTransport::instance(){static SerialTransport t;return t;}
esp_err_t SerialTransport::sendLine(const char *s){output.emplace_back(s);return ESP_OK;}
bool SerialTransport::receive(SerialEvent &e,TickType_t){
    if(nextInput==inputs.size()) throw Stop{};
    auto i=inputs[nextInput++];ticks=i.at;
    if(i.reset){e={};e.kind=SerialEventKind::Reset;return true;}
    if(i.line.empty()) return false;
    e={};e.length=uint16_t(i.line.size());std::memcpy(e.bytes,i.line.data(),e.length);return true;
}
HudStateStore &HudStateStore::instance(){static HudStateStore s;return s;}
void HudStateStore::publish(HudState s){current=s;}
HudState HudStateStore::snapshot()const{return current;}
DeviceConfig &DeviceConfig::instance(){static DeviceConfig c;return c;}
bool DeviceConfig::handleMessage(const cJSON *r,HlpSendLine,void*){
    const cJSON *t=cJSON_GetObjectItemCaseSensitive(r,"t");
    if(cJSON_IsString(t)&&std::strcmp(t->valuestring,"hi")==0)++configHi;
    return false;
}
void run(std::vector<Input> events){
    inputs=std::move(events);nextInput=0;output.clear();current={};configHi=0;ticks=1;
    HlpProtocol p;check(p.start()==ESP_OK,"start failed");
    try{p.run();}catch(const Stop&){}
}
int declarations(){
    int count=0;
    for(auto &line:output){
        check(line.size()+1<=HLP_MAX_FRAME,"TX exceeds LF frame budget");
        cJSON *r=cJSON_Parse(line.c_str());check(r,"bad outbound JSON");
        auto t=cJSON_GetObjectItemCaseSensitive(r,"t");
        if(cJSON_IsString(t)&&std::strcmp(t->valuestring,"dev")==0){
            ++count;auto want=cJSON_GetObjectItemCaseSensitive(r,"want");
            auto fields=cJSON_GetObjectItemCaseSensitive(want,"fields");bool lan=false;
            for(int j=0;j<cJSON_GetArraySize(fields);++j){auto f=cJSON_GetArrayItem(fields,j);if(cJSON_IsString(f)&&std::strcmp(f->valuestring,"lan")==0)lan=true;}
            check(lan,"dev must explicitly request lan");
            auto transport=cJSON_GetObjectItemCaseSensitive(r,"transport");
            check(cJSON_IsString(transport)&&std::strcmp(transport->valuestring,"usb")==0,"dev transport must be usb");
        }
        cJSON_Delete(r);
    }
    return count;
}
#define main core_test_main
#include "../tests/hlp_core_test.c"
#undef main
int main(){try{
    check(core_test_main()==0,"existing core regression");
    std::cout<<"PASS existing hlp_core_test.c\n";
    run({});check(declarations()==1,"startup missing dev declaration");
    std::cout<<"PASS startup explicit lan declaration\n";
    const std::string hi="{\"v\":1,\"t\":\"hi\",\"sess\":7,\"proto\":1,\"caps\":[\"device_config\"]}\n";
    const std::string hi2="{\"v\":1,\"t\":\"hi\",\"sess\":8,\"proto\":1}\n";
    run({{600,hi},{601,hi},{602,hi}});
    check(declarations()==2,"late legacy hi must trigger one dev, never dev/hi loop");
    check(configHi==1,"duplicate hi floods config");
    std::cout<<"PASS late hi recovery and bounded duplicate hi/config\n";
    run({{600,hi},{700,hi2},{701,hi2}});
    check(declarations()==3&&configHi==2,"fast reconnect new sess needs dev/config once");
    run({{600,hi},{700,"",true},{701,hi},{702,hi}});
    check(declarations()==4&&configHi==2,"UART reset must redeclare/reopen handshake");
    run({{600,hi},{10600,""},{10601,""},{10602,hi},{10603,hi}});
    check(declarations()==4&&configHi==2,"10s timeout must redeclare/reopen handshake");
    run({{600,hi},{700,"{\"v\":1,\"t\":\"bye\"}\n"},{701,hi},{702,hi}});
    check(declarations()==3&&configHi==2,"bye reconnect needs new declaration");
    std::cout<<"PASS reconnect: new sess, UART reset, timeout, bye\n";
    run({{600,"{\"v\":1,\"t\":\"hi\",\"sess\":4294967296}\n"},
         {700,"{\"v\":1,\"t\":\"hi\",\"sess\":4294967297}\n"},
         {701,"{\"v\":1,\"t\":\"hi\",\"sess\":4294967297}\n"}});
    check(declarations()==3&&configHi==2,"Android sess above uint32 must distinguish reconnect");
    std::cout<<"PASS wide Android session IDs\n";
    run({{600,hi},{700,"{\"v\":1,\"t\":\"s\",\"ts\":1,\"lan\":[[5,1],[32,32]]}\n"},{701,hi}});
    check(current.laneCount==2&&current.lanes[0].selectedMask==1,"negotiation reply lost decoded lanes");
    std::cout<<"PASS actual decoder lane payload survives duplicate hi\n";
    run({{0,hi},{10001,""},{10002,hi}});
    check(declarations()==4&&configHi==2,"tick-zero hi must still expire");
    run({{600,hi},{3601,""}});
    check(current.connected,"3s stale threshold must not end session");
    run({{600,hi},{10600,""}});
    check(declarations()==2,"timeout fired at exact 10s boundary");
    run({{600,"{\"v\":1,\"t\":\"hi\",\"proto\":2}\n"},
         {601,"{\"v\":1,\"t\":\"hi\",\"sess\":-1}\n"},
         {602,"{\"v\":1,\"t\":\"hi\",\"sess\":1.5}\n"},
         {603,"{\"v\":1,\"t\":\"ping\"}\n"}});
    check(declarations()==1&&configHi==0&&!current.connected,"invalid hi established session");
    check(output.back()=="{\"v\":1,\"t\":\"pong\"}","ping must still reply");
    run({{600,hi.substr(0,10)},{601,hi.substr(10)+hi}});
    check(declarations()==2&&configHi==1,"fragmented/coalesced hi handling");
    run({{50000,""},{100000,""}});
    check(declarations()==1&&configHi==0,"idle UART must not flood dev/config");
    std::cout<<"PASS timeout boundaries, invalid hi, ping, framing, idle silence\n";
    return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
'''
with tempfile.TemporaryDirectory(prefix='hlp-host-', dir=os.environ.get('TMPDIR')) as temp:
    build = Path(temp)
    for name, text in headers.items():
        p = build / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text)
    (build / 'test.cpp').write_text(harness)
    cjson = Path(args.idf) / 'components/json/cJSON'
    sources = [build / 'test.cpp',
               root / 'main/protocol/hlp_decoder.cpp', root / 'main/protocol/hlp_core.c', cjson / 'cJSON.c']
    includes = [build, root / 'main', cjson]
    exe = build / 'hlp_test.exe'
    if os.name == 'nt':
        vcvars = list(Path('C:/Program Files (x86)/Microsoft Visual Studio').glob('*/BuildTools/VC/Auxiliary/Build/vcvars64.bat'))
        assert vcvars, 'MSVC Build Tools required'
        command = subprocess.list2cmdline(['cl', '/nologo', '/EHsc', '/std:c++17', '/utf-8',
                    *['/I' + str(p) for p in includes], *map(str, sources), '/Fe:' + str(exe)])
        script = build / 'build.cmd'
        script.write_text(f'@call "{vcvars[-1]}" >nul\n@{command}\n')
        subprocess.run(['cmd.exe', '/d', '/c', str(script)], cwd=build, check=True)
    else:
        subprocess.run([shutil.which('g++') or 'g++', '-std=c++17', *['-I'+str(p) for p in includes],
                        *map(str, sources), '-o', str(exe)], cwd=build, check=True)
    subprocess.run([str(exe)], check=True)
