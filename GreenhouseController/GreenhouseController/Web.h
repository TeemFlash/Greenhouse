#pragma once
const char WEB_PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="ru"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Мини-теплица</title><style>
:root{color-scheme:dark;font:16px system-ui;background:#101b20;color:#e8f3ef}body{max-width:980px;margin:auto;padding:20px}h1{font-size:27px}section{background:#1b2b31;padding:18px;margin:16px 0;border-radius:18px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px}.card{background:#263b42;padding:16px;border-radius:12px}.value{font-size:30px;color:#77efba}button,input,select{font:inherit;padding:10px;border:1px solid #56716e;border-radius:8px;background:#13272b;color:inherit}button{cursor:pointer;margin:5px;background:#284d42}button.danger{background:#8d302a}label{display:block;margin:8px 0}input,select{box-sizing:border-box;width:100%}small{color:#adc6c0}#message{position:sticky;top:0;padding:12px;background:#27483e;white-space:pre-wrap}pre{white-space:pre-wrap;overflow-wrap:anywhere}details{margin:12px 0}a{color:#8be9c1}</style>
<h1>Мини-теплица</h1><div id="message" role="status">Подключение…</div>
<section><div class="grid" id="sensors"></div><p id="status"></p><button class="danger" onclick="action('stop')">Остановить всё</button><button onclick="action('resume')">Возобновить</button><button onclick="action('ack')">Сбросить ошибку полива</button><p><small>Ручное включение временное. Помпа работает не дольше одного импульса; защита по воде остаётся активной. При работе помпы веб-запросы ожидают её выключения.</small></p><div id="outputs"></div></section>
<section><h2>Настройки</h2><p>Режимы: 0 — выкл., 1 — ручной, 2 — автоматический. Время расписания задаётся в минутах от полуночи: 480 = 08:00, 1200 = 20:00. Одинаковые начало и конец отключают расписание.</p><form id="settings"><div class="grid" id="fields"></div><button>Сохранить настройки</button></form><small>Сохранение останавливает нагрузки. После проверки параметров нажмите «Возобновить». Ручные команды после перезапуска не восстанавливаются.</small></section>
<section><h2>Калибровка</h2><p id="raw"></p><p>Последовательно подготовьте датчик к каждому состоянию и сохраните показание. Между крайними значениями должно быть не менее 500 отсчётов АЦП. После калибровки возобновите систему.</p><button onclick="cal(0)">Почва: сухо</button><button onclick="cal(1)">Почва: влажно</button><button onclick="cal(2)">Бак: пусто</button><button onclick="cal(3)">Бак: полный</button><small>Проценты относительные. При сохранении «влажно» используйте влажную почву; электронику датчика не погружайте.</small></section>
<section><h2>Wi-Fi</h2><p id="network"></p><form id="wifi"><label>SSID<input name="ssid" maxlength="32" required autocomplete="off"></label><label>Пароль сети<input type="password" name="pass" maxlength="64" autocomplete="new-password"></label><button>Сохранить и подключиться</button></form><small>Точка доступа устройства остаётся доступной. Для входа в панель: пользователь admin, пароль — ключ с экрана NETWORK или из монитора порта. Интернет нужен для установки часов, управление выполняется локально.</small></section>
<section><h2>Журнал</h2><a href="/download?file=data">Показания CSV</a> · <a href="/download?file=events">События CSV</a> · <a href="/download?file=dataOld">Предыдущие показания</a> · <a href="/download?file=eventsOld">Предыдущие события</a><pre id="events"></pre><small>Скачивание доступно при общей остановке. Времена в CSV — UTC либо uptime до синхронизации. Запись ограничена двумя файлами по ≈1 МБ для каждого журнала.</small></section>
<script>
const $=id=>document.getElementById(id);let busy=false,settingsLoaded=false;
const names=['Помпа','Кулеры','Лампа','Лента'];
const labels={pumpMode:'Режим помпы',fanMode:'Режим кулеров',lampMode:'Режим лампы',stripMode:'Режим ленты',soilLow:'Начало полива, %',soilTarget:'Цель полива, %',waterMin:'Минимум воды, %',pulseSec:'Импульс помпы, с',soakSec:'Впитывание, с',cycles:'Максимум циклов',maxPumpSec:'Суммарный полив, с',intervalMin:'Интервал поливов, мин',tempOn:'Вентиляция вкл., °C',tempOff:'Вентиляция выкл., °C',humOn:'Вентиляция вкл., RH %',humOff:'Вентиляция выкл., RH %',ventEveryMin:'Период проветривания, мин (0 = нет)',ventSec:'Проветривание, с',fanFallback:'Вентиляция при ошибке (0/1)',lampStart:'Лампа: начало, мин',lampEnd:'Лампа: конец, мин',stripStart:'Лента: начало, мин',stripEnd:'Лента: конец, мин',lampFallback:'Лампа без часов (0/1)',stripFallback:'Лента без часов (0/1)',manualSec:'Таймер ручного режима, с',logSec:'Интервал CSV, с',pageSec:'Смена экрана, с',timezoneMin:'Смещение UTC, мин',bootAuto:'Автоматика после перезапуска (0/1)',soilDry:'Сухая почва, АЦП',soilWet:'Влажная почва, АЦП',waterEmpty:'Пустой бак, АЦП',waterFull:'Полный бак, АЦП',touchHigh:'Активный уровень Touch (0/1)'};
async function req(url,data){const options=data?{method:'POST',headers:{'X-Greenhouse':'1'},body:new URLSearchParams(data)}:{};const r=await fetch(url,options);if(!r.ok)throw Error(await r.text());return r;}
async function action(cmd,ch,on){try{$('message').textContent=await(await req('/action',{cmd,ch:ch??'',on:on?1:0})).text();await refresh();}catch(e){$('message').textContent=e.message;}}
async function cal(point){try{$('message').textContent=await(await req('/calibrate',{point})).text();await loadSettings();}catch(e){$('message').textContent=e.message;}}
function value(v,u){return v===null?'—':v.toFixed(1)+u;}
async function refresh(){if(busy)return;busy=true;try{const s=await(await req('/state')).json();$('sensors').replaceChildren();for(const [name,v,u] of [['Воздух',s.temp,' °C'],['Влажность воздуха',s.humidity,' %'],['Почва',s.soil,' %'],['Вода',s.water,' %']]){const c=document.createElement('div');c.className='card';const a=document.createElement('div');a.textContent=name;const b=document.createElement('div');b.className='value';b.textContent=value(v,u);c.append(a,b);$('sensors').append(c);}
$('status').textContent=`${s.paused?'ОСТАНОВЛЕНО':'РАБОТАЕТ'} · ${s.problem} · SD ${s.sd?'OK':'нет'} · ${s.clock} · heap ${s.heap} байт`;
$('outputs').replaceChildren();s.outputs.forEach((on,i)=>{const d=document.createElement('p');d.textContent=`${names[i]}: команда ${on?'ВКЛ':'ВЫКЛ'}${s.io?'':' / связь потеряна'} `;for(const [txt,v] of [['Включить',true],['Выключить',false]]){const b=document.createElement('button');b.textContent=txt;b.onclick=()=>action('manual',i,v);d.append(b);}const b=document.createElement('button');b.textContent='Вернуть режим';b.onclick=()=>action('release',i);d.append(b);$('outputs').append(d);});
$('raw').textContent=`АЦП почва: ${s.soilRaw}, вода: ${s.waterRaw}. Калибровка: почва ${s.soilCal?'OK':'нет'}, вода ${s.waterCal?'OK':'нет'}.`;
$('network').textContent=`AP: ${s.ap}, 192.168.4.1; Wi-Fi: ${s.wifi?s.ip:'не подключён'}`;$('events').textContent=s.events.join('\n')+`\nПропущенных событий SD: ${s.dropped}`;
if(!settingsLoaded){await loadSettings();$('message').textContent='Подключено. Показания реальные; «—» означает отсутствие достоверных данных.';}
}catch(e){$('message').textContent='Нет ответа: '+e.message;}finally{busy=false;}}
async function loadSettings(){const a=await(await req('/settings')).json();$('fields').replaceChildren();for(const f of a){const l=document.createElement('label');l.textContent=labels[f.key]||f.label;const i=document.createElement('input');i.type='number';i.name=f.key;i.value=f.value;i.min=f.min;i.max=f.max;i.step=1;i.required=true;l.append(i);$('fields').append(l);}settingsLoaded=true;}
$('settings').onsubmit=async e=>{e.preventDefault();try{$('message').textContent=await(await req('/settings',Object.fromEntries(new FormData(e.target)))).text();}catch(e){$('message').textContent=e.message;}};
$('wifi').onsubmit=async e=>{e.preventDefault();try{$('message').textContent=await(await req('/wifi',Object.fromEntries(new FormData(e.target)))).text();e.target.pass.value='';}catch(e){$('message').textContent=e.message;}};
refresh();setInterval(refresh,2500);
</script></html>)HTML";

bool authorized(bool mutate=false) {
  if(!server.authenticate("admin",accessKey)){server.requestAuthentication();return false;}
  if(mutate&&server.header("X-Greenhouse")!="1"){server.send(403,"text/plain","Missing request header");return false;}
  server.sendHeader("Cache-Control","no-store");
  server.sendHeader("X-Content-Type-Options","nosniff");
  return true;
}
String jsonNumber(float n) {return isfinite(n)?String(n,1):String("null");}
bool integerArg(const char *name,int &out) {
  if(!server.hasArg(name))return false;String s=server.arg(name);
  if(s.isEmpty()||s.length()>10)return false;
  char *end;long n=strtol(s.c_str(),&end,10);
  if(*end || n< -1000000 || n>1000000)return false;out=int(n);return true;
}
void setupWeb() {
  server.collectHeaders("X-Greenhouse");
  server.on("/",HTTP_GET,[]{if(authorized())server.send_P(200,"text/html; charset=utf-8",WEB_PAGE);});
  server.on("/state",HTTP_GET,[]{
    if(!authorized())return;
    String j; j.reserve(2300);uint32_t now=millis();
    j="{\"temp\":"+jsonNumber(readings.air&&now-readings.airAt<5000?readings.temperature:NAN);
    j+=",\"humidity\":"+jsonNumber(readings.air&&now-readings.airAt<5000?readings.humidity:NAN);
    j+=",\"soil\":"+jsonNumber(readings.soil&&now-readings.soilAt<3000?readings.soilPct:NAN);
    j+=",\"water\":"+jsonNumber(readings.water&&now-readings.waterAt<3000?readings.waterPct:NAN);
    j+=",\"soilRaw\":"+String(readings.soilRaw)+",\"waterRaw\":"+String(readings.waterRaw);
    j+=",\"soilCal\":"+String(soilCal(cfg)?"true":"false")+",\"waterCal\":"+String(waterCal(cfg)?"true":"false");
    j+=",\"paused\":"+String(control.paused?"true":"false")+",\"io\":"+String(pcfOK&&!control.ioFault?"true":"false");
    j+=",\"sd\":"+String(sdOK?"true":"false")+",\"wifi\":"+String(WiFi.status()==WL_CONNECTED?"true":"false");
    j+=",\"ip\":\""+WiFi.localIP().toString()+"\",\"ap\":\""+String(apName)+"\",\"problem\":\""+problem()+"\"";
    char clock[24]="not set";time_t t=time(nullptr);if(t>=1700000000){tm lt;localtime_r(&t,&lt);strftime(clock,sizeof(clock),"%Y-%m-%d %H:%M:%S",&lt);}
    j+=",\"clock\":\""+String(clock)+"\",\"heap\":"+String(ESP.getFreeHeap())+",\"dropped\":"+String(droppedEvents)+",\"outputs\":[";
    for(int i=0;i<4;i++){if(i)j+=',';j+=control.out[i]?"true":"false";}
    j+="],\"events\":[";
    for(int i=0;i<eventCount;i++){if(i)j+=',';const Event &e=events[(eventHead+12-eventCount+i)%12];j+='"';j+=String(e.uptime/1000)+"s "+e.name;j+='"';}
    j+="]}";server.send(200,"application/json",j);
  });
  server.on("/settings",HTTP_GET,[]{
    if(!authorized())return;Field f[40];int n=fields(cfg,f);String j="[";j.reserve(4500);
    for(int i=0;i<n;i++){if(i)j+=',';j+="{\"key\":\""+String(f[i].key)+"\",\"label\":\""+f[i].label+"\",\"value\":"+String(*f[i].value)+",\"min\":"+String(f[i].lo)+",\"max\":"+String(f[i].hi)+"}";}
    j+=']';server.send(200,"application/json",j);
  });
  server.on("/settings",HTTP_POST,[]{
    if(!authorized(true))return;Settings next=cfg;Field f[40];int n=fields(next,f);
    for(int i=0;i<n;i++)if(server.hasArg(f[i].key)) {
      int v;if(!integerArg(f[i].key,v)){server.send(400,"text/plain","Invalid number");return;}
      *f[i].value=v;
    }
    if(!valid(next)){server.send(400,"text/plain","Invalid limits: start < target, OFF < ON, pulse <= total");return;}
    if(next.soilDry!=cfg.soilDry)next.calMask|=1;
    if(next.soilWet!=cfg.soilWet)next.calMask|=2;
    if(next.waterEmpty!=cfg.waterEmpty)next.calMask|=4;
    if(next.waterFull!=cfg.waterFull)next.calMask|=8;
    stopSystem();cfg=next;bool ok=saveSettings();setClock();editing=false;
    server.send(ok?200:500,"text/plain; charset=utf-8",ok?"Сохранено. Система остановлена — проверьте настройки и возобновите.":"Ошибка записи настроек");
  });
  server.on("/action",HTTP_POST,[]{
    if(!authorized(true))return;String cmd=server.arg("cmd");bool ok=false;int ch,on;
    if(cmd=="stop"){stopSystem();ok=true;}
    else if(cmd=="resume")ok=resumeSystem();
    else if(cmd=="ack"){control.acknowledge();event("PUMP FAULT ACK");ok=true;}
    else if(cmd=="manual"&&integerArg("ch",ch)&&integerArg("on",on)&&(on==0||on==1))ok=manualCommand(ch,on);
    else if(cmd=="release"&&integerArg("ch",ch)&&ch>=0&&ch<4){control.overrideMode[ch]=-1;control.manual[ch]=false;if(ch==PUMP)control.finish(millis());ok=true;}
    server.send(ok?200:409,"text/plain; charset=utf-8",ok?"Команда принята":"Команда отклонена: проверьте остановку, связь, калибровку, воду и ошибку полива");
  });
  server.on("/calibrate",HTTP_POST,[]{
    if(!authorized(true))return;int point;bool ok=integerArg("point",point)&&capture(point);
    server.send(ok?200:409,"text/plain; charset=utf-8",ok?"Показание сохранено. Проверьте второй предел калибровки.":"Нет свежего показания или ошибка записи");
  });
  server.on("/wifi",HTTP_POST,[]{
    if(!authorized(true))return;String ssid=server.arg("ssid"),pass=server.arg("pass");
    if(ssid.length()<1||ssid.length()>32||pass.length()>64||(pass.length()>0&&pass.length()<8)) {server.send(400,"text/plain","Invalid SSID/password length");return;}
    stopSystem();ssid.toCharArray(wifiSsid,sizeof(wifiSsid));pass.toCharArray(wifiPass,sizeof(wifiPass));bool ok=saveSettings();
    WiFi.begin(wifiSsid,wifiPass);server.send(ok?200:500,"text/plain; charset=utf-8",ok?"Подключение запущено, точка доступа остаётся включённой.":"Ошибка записи");
  });
  server.on("/download",HTTP_GET,[]{
    if(!authorized())return;
    if(!control.paused){server.send(409,"text/plain; charset=utf-8","Сначала остановите систему");return;}
    if(!sdOK){server.send(503,"text/plain","SD unavailable");return;}
    String name=server.arg("file");const char *path=name=="data"?"/data.csv":name=="events"?"/events.csv":name=="dataOld"?"/data.old":name=="eventsOld"?"/events.old":nullptr;
    if(!path){server.send(400,"text/plain","Unknown file");return;}
    File f=SD.open(path,FILE_READ);if(!f){server.send(404,"text/plain","No log yet");return;}
    server.sendHeader("Content-Disposition",String("attachment; filename=\"")+(path+1)+"\"");
    server.streamFile(f,"text/csv");f.close();
  });
  server.onNotFound([]{server.send(404,"text/plain","Not found");});
}
