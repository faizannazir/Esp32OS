#include "webportal.h"
#if WEB_PORTAL_ENABLED

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_http_server.h"
#include "esp_system.h"
#include "os_logging.h"
#include "os_networking.h"
#include "os_kernel.h"
#include "os_ota.h"
#include "os_shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "WEBPORTAL"

#define TELEMETRY_HISTORY_DEPTH 24
#define TELEMETRY_SAMPLE_PERIOD_MS 5000

static httpd_handle_t server = NULL;

typedef struct {
    kernel_stats_t kernel;
    process_t      processes[OS_MAX_PROCESSES];
    int            process_count;
    uint32_t       ts_s;
} telemetry_sample_t;

static telemetry_sample_t s_samples[TELEMETRY_HISTORY_DEPTH];
static size_t s_sample_head = 0;
static size_t s_sample_count = 0;
static bool   s_sampler_started = false;

static const char *proc_state_text(proc_state_t state)
{
    switch (state) {
    case PROC_STATE_RUNNING:   return "RUN";
    case PROC_STATE_READY:     return "RDY";
    case PROC_STATE_BLOCKED:    return "BLK";
    case PROC_STATE_SUSPENDED:  return "SUSP";
    case PROC_STATE_DELETED:    return "DEAD";
    default:                   return "UNK";
    }
}

static bool parse_param_u32(const char *body, const char *key, uint32_t *out)
{
    if (!body || !key || !out) return false;
    const char *pos = strstr(body, key);
    if (!pos) return false;
    pos += strlen(key);
    if (*pos != '=') return false;
    pos++;
    *out = (uint32_t)strtoul(pos, NULL, 10);
    return true;
}

static bool parse_param_str(const char *body, const char *key, char *out, size_t out_sz)
{
    if (!body || !key || !out || out_sz == 0) return false;
    const char *pos = strstr(body, key);
    if (!pos) return false;
    pos += strlen(key);
    if (*pos != '=') return false;
    pos++;
    size_t i = 0;
    while (*pos && *pos != '&' && i + 1 < out_sz) {
        out[i++] = *pos++;
    }
    out[i] = '\0';
    return true;
}

static esp_err_t send_text(httpd_req_t *req, const char *text)
{
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t send_json(httpd_req_t *req, const char *text)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
}

static void telemetry_capture_sample(void)
{
    telemetry_sample_t *sample = &s_samples[s_sample_head];
    memset(sample, 0, sizeof(*sample));

    os_kernel_get_stats(&sample->kernel);
    sample->process_count = os_process_list(sample->processes, OS_MAX_PROCESSES);
    sample->ts_s = sample->kernel.uptime_ms / 1000;

    s_sample_head = (s_sample_head + 1) % TELEMETRY_HISTORY_DEPTH;
    if (s_sample_count < TELEMETRY_HISTORY_DEPTH) {
        s_sample_count++;
    }
}

static void telemetry_sampler_task(void *arg)
{
    (void)arg;
    while (1) {
        telemetry_capture_sample();
        vTaskDelay(pdMS_TO_TICKS(TELEMETRY_SAMPLE_PERIOD_MS));
    }
}

static const telemetry_sample_t *telemetry_get_sample(size_t index_from_oldest)
{
    if (index_from_oldest >= s_sample_count) return NULL;
    size_t start = (s_sample_head + TELEMETRY_HISTORY_DEPTH - s_sample_count) % TELEMETRY_HISTORY_DEPTH;
    size_t idx = (start + index_from_oldest) % TELEMETRY_HISTORY_DEPTH;
    return &s_samples[idx];
}

static esp_err_t handle_root(httpd_req_t *req)
{
    const char *html =
        "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>ESP32OS Portal</title>"
        "<style>body{font-family:system-ui,sans-serif;margin:0;background:#f5f7fb;color:#111}.wrap{max-width:1200px;margin:0 auto;padding:16px}.hero{display:flex;justify-content:space-between;align-items:end;gap:12px;flex-wrap:wrap}"
        ".cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:12px;margin:16px 0}.card{background:#fff;border:1px solid #dde4f0;border-radius:14px;padding:14px;box-shadow:0 1px 2px rgba(0,0,0,.03)}"
        ".label{font-size:12px;text-transform:uppercase;letter-spacing:.08em;color:#667}.value{font-size:24px;font-weight:700;margin-top:6px}.sub{color:#667;font-size:13px}.grid{display:grid;grid-template-columns:1.15fr .85fr;gap:16px;align-items:start}"
        "@media(max-width:980px){.grid{grid-template-columns:1fr}}table{width:100%;border-collapse:collapse;background:#fff;border:1px solid #dde4f0;border-radius:14px;overflow:hidden}th,td{padding:8px 10px;border-bottom:1px solid #eef2f7;text-align:left;font-size:13px}th{background:#f8fafc}button{border:0;border-radius:10px;padding:8px 10px;background:#0f62fe;color:#fff;cursor:pointer}button.secondary{background:#334155}button.danger{background:#c2410c}input{padding:9px 10px;border:1px solid #cbd5e1;border-radius:10px;width:100%;box-sizing:border-box}pre{background:#111827;color:#d1fae5;padding:12px;border-radius:14px;min-height:120px;overflow:auto;white-space:pre-wrap}.row{display:flex;gap:8px;flex-wrap:wrap}.row>*{flex:1 1 160px}.chart{width:100%;height:180px;background:#fff;border:1px solid #dde4f0;border-radius:14px}.drawer{position:sticky;top:16px;background:#fff;border:1px solid #dde4f0;border-radius:16px;padding:14px;box-shadow:0 1px 2px rgba(0,0,0,.03)}.drawer.hidden{display:none}.drawer-head{display:flex;justify-content:space-between;gap:12px;align-items:center}.task-line{font-size:13px;color:#334155;margin:6px 0}.tiny{font-size:12px;color:#667}</style>"
        "</head><body><div class=\"wrap\">"
        "<div class=\"hero\"><div><h1 style=\"margin:0\">ESP32OS Portal</h1><div class=\"sub\">On-device control for telemetry, commands, tasks, and firmware actions.</div></div><div class=\"sub\" id=\"netlabel\">Starting...</div></div>"
        "<div class=\"cards\"><div class=\"card\"><div class=\"label\">Heap</div><div class=\"value\" id=\"heap\">-</div><div class=\"sub\" id=\"heap2\">-</div></div><div class=\"card\"><div class=\"label\">CPU</div><div class=\"value\" id=\"cpu\">-</div><div class=\"sub\" id=\"uptime\">-</div></div><div class=\"card\"><div class=\"label\">Tasks</div><div class=\"value\" id=\"tasks\">-</div><div class=\"sub\" id=\"tasks2\">-</div></div><div class=\"card\"><div class=\"label\">OTA</div><div class=\"value\" id=\"ota\">-</div><div class=\"sub\" id=\"ota2\">-</div></div></div>"
        "<div class=\"grid\"><div><h2>Live KPIs</h2><canvas class=\"chart\" id=\"heapChart\" width=\"900\" height=\"180\"></canvas><div style=\"height:10px\"></div><canvas class=\"chart\" id=\"cpuChart\" width=\"900\" height=\"180\"></canvas><div style=\"height:18px\"></div><h2>Processes</h2><table><thead><tr><th>PID</th><th>Name</th><th>Status</th><th>Prio</th><th>Stack</th><th>Runtime</th><th>Actions</th></tr></thead><tbody id=\"ps\"></tbody></table></div>"
        "<div><div id=\"drawer\" class=\"drawer hidden\"><div class=\"drawer-head\"><div><h2 style=\"margin:0\">Task Detail</h2><div class=\"tiny\" id=\"drawerSub\">Select a task</div></div><button class=\"secondary\" onclick=\"closeDrawer()\">Close</button></div><div id=\"drawerBody\"></div><div style=\"height:12px\"></div><canvas class=\"chart\" id=\"taskChart\" width=\"420\" height=\"180\"></canvas><div style=\"height:10px\"></div><canvas class=\"chart\" id=\"taskRuntimeChart\" width=\"420\" height=\"180\"></canvas></div><h2>Control</h2><div class=\"card\"><div class=\"row\"><input id=\"cmd\" placeholder=\"help, ps, top, ota status\"><button onclick=\"runCommand()\">Run</button></div><div style=\"height:10px\"></div><pre id=\"out\"></pre></div><div style=\"height:12px\"></div><div class=\"card\"><div class=\"row\"><button class=\"secondary\" onclick=\"otaConfirm()\">Confirm Firmware</button><button class=\"danger\" onclick=\"otaRollback()\">Rollback Firmware</button><button class=\"danger\" onclick=\"reboot()\">Reboot</button></div></div></div></div></div>"
        "<script>const state={telemetry:[],selectedPid:null};"
        "async function postText(url, body){const r=await fetch(url,{method:'POST',headers:{'Content-Type':'text/plain'},body});return await r.text();}"
        "async function runCommand(){const cmd=document.getElementById('cmd').value;const out=await postText('/api/command',cmd);document.getElementById('out').textContent=out;refresh();}"
        "async function otaConfirm(){document.getElementById('out').textContent=await postText('/api/ota/confirm','');refresh();}"
        "async function otaRollback(){document.getElementById('out').textContent=await postText('/api/ota/rollback','');}"
        "async function reboot(){document.getElementById('out').textContent=await postText('/api/system/reboot','');}"
        "async function procAction(pid, action){document.getElementById('out').textContent=await postText('/api/process/action','pid='+pid+'&action='+action);refresh();}"
        "async function openTask(pid){state.selectedPid=pid;const data=await (await fetch('/api/process/history?pid='+pid)).json();document.getElementById('drawer').classList.remove('hidden');document.getElementById('drawerSub').textContent=data.name+' (pid '+data.pid+')';document.getElementById('drawerBody').innerHTML='<div class=\"task-line\">Samples: '+data.samples.length+'</div><div class=\"task-line\">Latest stack high-water and runtime history for the selected task.</div>';drawTaskHistory(data.samples);}" 
        "function closeDrawer(){state.selectedPid=null;document.getElementById('drawer').classList.add('hidden');}"
        "async function refresh(){const s=await (await fetch('/api/status')).json();const p=await (await fetch('/api/processes')).json();const o=await (await fetch('/api/ota')).json();const t=await (await fetch('/api/telemetry/history')).json();renderStatus(s,o);renderProcesses(p);state.telemetry=t.samples;drawTelemetry(t.samples);if(state.selectedPid!=null){try{const data=await (await fetch('/api/process/history?pid='+state.selectedPid)).json();drawTaskHistory(data.samples);}catch(e){}}}"
        "function renderStatus(s,o){document.getElementById('heap').textContent=s.free_heap+' / '+s.total_heap;document.getElementById('heap2').textContent='min free '+s.min_free_heap+' | largest block '+s.largest_free_block;document.getElementById('cpu').textContent=s.cpu_load_pct+'%';document.getElementById('uptime').textContent='uptime '+s.uptime_s+'s';document.getElementById('tasks').textContent=s.procs;document.getElementById('tasks2').textContent=(s.net.ip ? s.net.ip : 'no net');document.getElementById('ota').textContent=o.needs_confirmation ? 'Pending' : 'OK';document.getElementById('ota2').textContent=o.can_rollback ? 'rollback ready' : 'no rollback';document.getElementById('netlabel').textContent=s.net.ip ? ('Portal URL: http://'+s.net.ip+'/') : 'Portal active, waiting for network address';}"
        "function renderProcesses(items){const tb=document.getElementById('ps');tb.innerHTML='';items.forEach(p=>{const tr=document.createElement('tr');tr.style.cursor='pointer';tr.onclick=()=>openTask(p.pid);tr.innerHTML='<td>'+p.pid+'</td><td>'+p.name+'</td><td>'+p.state+'</td><td>'+p.priority+'</td><td>'+p.stack_high_water+'</td><td>'+p.runtime_ticks+'</td><td><div class=\"row\" onclick=\"event.stopPropagation()\"><button class=\"secondary\" onclick=\"procAction('+p.pid+',\\'suspend\\')\">Suspend</button><button class=\"secondary\" onclick=\"procAction('+p.pid+',\\'resume\\')\">Resume</button><button class=\"danger\" onclick=\"procAction('+p.pid+',\\'kill\\')\">Kill</button></div></td>';tb.appendChild(tr);});}"
        "function drawTelemetry(samples){const heapCanvas=document.getElementById('heapChart');const cpuCanvas=document.getElementById('cpuChart');drawLineChart(heapCanvas,samples.map(s=>s.free_heap), '#0f62fe');drawLineChart(cpuCanvas,samples.map(s=>s.cpu_load_pct), '#c2410c', 0, 100);}"
        "function drawTaskHistory(samples){drawLineChart(document.getElementById('taskChart'),samples.map(s=>s.stack_high_water),'#16a34a');drawLineChart(document.getElementById('taskRuntimeChart'),samples.map(s=>s.runtime_ticks),'#7c3aed');}"
        "function drawLineChart(canvas, values, color, forcedMin, forcedMax){const ctx=canvas.getContext('2d');const w=canvas.width,h=canvas.height;ctx.clearRect(0,0,w,h);ctx.fillStyle='#fff';ctx.fillRect(0,0,w,h);ctx.strokeStyle='#dbe4f0';ctx.lineWidth=1;for(let i=0;i<4;i++){const y=12+i*(h-24)/3;ctx.beginPath();ctx.moveTo(32,y);ctx.lineTo(w-12,y);ctx.stroke();}if(!values.length){ctx.fillStyle='#667';ctx.fillText('No samples yet',16,20);return;}const min=(forcedMin===undefined||forcedMin===null)?Math.min(...values):forcedMin;const max=(forcedMax===undefined||forcedMax===null)?Math.max(...values):forcedMax;const span=(max-min)||1;ctx.strokeStyle=color;ctx.lineWidth=2;ctx.beginPath();values.forEach((v,i)=>{const x=32+i*((w-48)/Math.max(values.length-1,1));const y=(h-16)-((v-min)/span)*(h-28);if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);});ctx.stroke();ctx.fillStyle='#667';ctx.fillText(String(max),4,16);ctx.fillText(String(min),4,h-8);}"
        "refresh();setInterval(refresh,2500);</script></body></html>";

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handle_status(httpd_req_t *req)
{
    kernel_stats_t st;
    os_kernel_get_stats(&st);
    os_net_status_t netst = {0};
    os_wifi_get_status(&netst);

    char buf[640];
    snprintf(buf, sizeof(buf),
             "{\"free_heap\":%u,\"total_heap\":%u,\"min_free_heap\":%u,\"largest_free_block\":%u,\"cpu_load_pct\":%u,\"procs\":%u,\"uptime_s\":%u,\"net\":{\"connected\":%s,\"ip\":\"%s\"}}",
             (unsigned)st.free_heap_bytes, (unsigned)st.total_heap_bytes,
             (unsigned)st.min_free_heap_bytes, (unsigned)st.largest_free_block,
             (unsigned)st.cpu_load_pct, (unsigned)st.process_count,
             (unsigned)(st.uptime_ms / 1000), netst.connected ? "true" : "false",
             netst.connected ? netst.ip : "");
    return send_json(req, buf);
}

static esp_err_t handle_processes(httpd_req_t *req)
{
    process_t procs[OS_MAX_PROCESSES];
    int cnt = os_process_list(procs, OS_MAX_PROCESSES);
    char *buf = calloc(1, 4096);
    if (!buf) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    }

    size_t off = 0;
    off += snprintf(buf + off, 4096 - off, "[");
    for (int i = 0; i < cnt && off < 4000; i++) {
        off += snprintf(buf + off, 4096 - off,
                        "{\"pid\":%u,\"name\":\"%s\",\"state\":\"%s\",\"priority\":%u,\"stack_high_water\":%u,\"runtime_ticks\":%llu,\"is_system\":%s}%s",
                        (unsigned)procs[i].pid, procs[i].name, proc_state_text(procs[i].state),
                        (unsigned)procs[i].priority, (unsigned)procs[i].stack_high_water,
                        (unsigned long long)procs[i].runtime_ticks,
                        procs[i].is_system ? "true" : "false",
                        (i + 1 < cnt) ? "," : "");
    }
    snprintf(buf + off, 4096 - off, "]");
    esp_err_t ret = send_json(req, buf);
    free(buf);
    return ret;
}

static esp_err_t handle_telemetry_history(httpd_req_t *req)
{
    char *buf = calloc(1, 4096);
    if (!buf) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    }

    size_t off = 0;
    off += snprintf(buf + off, 4096 - off, "{\"samples\":[");
    for (size_t i = 0; i < s_sample_count && off < 4000; i++) {
        const telemetry_sample_t *sample = telemetry_get_sample(i);
        if (!sample) break;
        off += snprintf(buf + off, 4096 - off,
                        "{\"ts_s\":%u,\"free_heap\":%u,\"cpu_load_pct\":%u,\"procs\":%u}%s",
                        (unsigned)sample->ts_s,
                        (unsigned)sample->kernel.free_heap_bytes,
                        (unsigned)sample->kernel.cpu_load_pct,
                        (unsigned)sample->process_count,
                        (i + 1 < s_sample_count) ? "," : "");
    }
    snprintf(buf + off, 4096 - off, "]}");
    esp_err_t ret = send_json(req, buf);
    free(buf);
    return ret;
}

static esp_err_t handle_process_history(httpd_req_t *req)
{
    char query[64] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing pid");
    }

    char pid_str[16] = {0};
    if (httpd_query_key_value(query, "pid", pid_str, sizeof(pid_str)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing pid");
    }

    os_pid_t pid = (os_pid_t)strtoul(pid_str, NULL, 10);
    char *buf = calloc(1, 4096);
    if (!buf) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    }

    const char *name = "unknown";
    size_t off = 0;
    off += snprintf(buf + off, 4096 - off, "{\"pid\":%u,\"name\":\"", (unsigned)pid);

    bool found_name = false;
    for (size_t i = 0; i < s_sample_count && !found_name; i++) {
        const telemetry_sample_t *sample = telemetry_get_sample(i);
        if (!sample) break;
        for (int j = 0; j < sample->process_count; j++) {
            if (sample->processes[j].pid == pid) {
                name = sample->processes[j].name;
                found_name = true;
                break;
            }
        }
    }
    off += snprintf(buf + off, 4096 - off, "%s\",\"samples\":[", name);

    bool first = true;
    for (size_t i = 0; i < s_sample_count && off < 4000; i++) {
        const telemetry_sample_t *sample = telemetry_get_sample(i);
        if (!sample) break;
        for (int j = 0; j < sample->process_count; j++) {
            if (sample->processes[j].pid != pid) continue;
            off += snprintf(buf + off, 4096 - off,
                            "%s{\"ts_s\":%u,\"stack_high_water\":%u,\"runtime_ticks\":%llu,\"state\":\"%s\",\"priority\":%u}",
                            first ? "" : ",",
                            (unsigned)sample->ts_s,
                            (unsigned)sample->processes[j].stack_high_water,
                            (unsigned long long)sample->processes[j].runtime_ticks,
                            proc_state_text(sample->processes[j].state),
                            (unsigned)sample->processes[j].priority);
            first = false;
            break;
        }
    }

    snprintf(buf + off, 4096 - off, "]}");
    esp_err_t ret = send_json(req, buf);
    free(buf);
    return ret;
}

static esp_err_t handle_ota(httpd_req_t *req)
{
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"can_rollback\":%s,\"needs_confirmation\":%s}",
             os_ota_can_rollback() ? "true" : "false",
             os_ota_needs_confirmation() ? "true" : "false");
    return send_json(req, buf);
}

static esp_err_t handle_process_action(httpd_req_t *req)
{
    char body[128] = {0};
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len < 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad request");
    }

    uint32_t pid = 0;
    char action[16] = {0};
    if (!parse_param_u32(body, "pid", &pid) || !parse_param_str(body, "action", action, sizeof(action))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing pid/action");
    }

    esp_err_t ret = ESP_OK;
    if (strcmp(action, "kill") == 0) {
        ret = os_process_kill((os_pid_t)pid);
    } else if (strcmp(action, "suspend") == 0) {
        ret = os_process_suspend((os_pid_t)pid);
    } else if (strcmp(action, "resume") == 0) {
        ret = os_process_resume((os_pid_t)pid);
    } else {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "unknown action");
    }

    if (ret != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(ret));
    }

    char resp[96];
    snprintf(resp, sizeof(resp), "ok pid=%u action=%s", (unsigned)pid, action);
    return send_text(req, resp);
}

static esp_err_t handle_command(httpd_req_t *req)
{
    char body[256] = {0};
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len < 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad request");
    }

    char output[2048];
    int cmd_ret = 0;
    esp_err_t ret = shell_execute_capture(body, output, sizeof(output), &cmd_ret);
    if (ret != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(ret));
    }

    if (output[0] == '\0') {
        snprintf(output, sizeof(output), "command returned %d but produced no text\n", cmd_ret);
    }

    return send_text(req, output);
}

static esp_err_t handle_ota_confirm(httpd_req_t *req)
{
    esp_err_t ret = os_ota_confirm();
    if (ret != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(ret));
    return send_text(req, "firmware confirmed");
}

static esp_err_t handle_ota_rollback(httpd_req_t *req)
{
    esp_err_t ret = os_ota_rollback();
    if (ret != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(ret));
    return send_text(req, "rollback requested; device rebooting");
}

static esp_err_t handle_reboot(httpd_req_t *req)
{
    send_text(req, "rebooting");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
    return ESP_OK;
}

esp_err_t webportal_start(void)
{
    if (server) return ESP_OK;

    if (!s_sampler_started) {
        telemetry_capture_sample();
        if (xTaskCreate(telemetry_sampler_task, "telemetry_sampler", 4096, NULL, 3, NULL) == pdPASS) {
            s_sampler_started = true;
        } else {
            OS_LOGW(TAG, "Telemetry sampler task could not start");
        }
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 12;
    config.stack_size = 8192;

    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        OS_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        server = NULL;
        return ret;
    }

    httpd_uri_t uri_root = { .uri = "/", .method = HTTP_GET, .handler = handle_root };
    httpd_uri_t uri_status = { .uri = "/api/status", .method = HTTP_GET, .handler = handle_status };
    httpd_uri_t uri_processes = { .uri = "/api/processes", .method = HTTP_GET, .handler = handle_processes };
    httpd_uri_t uri_telemetry = { .uri = "/api/telemetry/history", .method = HTTP_GET, .handler = handle_telemetry_history };
    httpd_uri_t uri_process_history = { .uri = "/api/process/history", .method = HTTP_GET, .handler = handle_process_history };
    httpd_uri_t uri_ota = { .uri = "/api/ota", .method = HTTP_GET, .handler = handle_ota };
    httpd_uri_t uri_process_action = { .uri = "/api/process/action", .method = HTTP_POST, .handler = handle_process_action };
    httpd_uri_t uri_command = { .uri = "/api/command", .method = HTTP_POST, .handler = handle_command };
    httpd_uri_t uri_ota_confirm = { .uri = "/api/ota/confirm", .method = HTTP_POST, .handler = handle_ota_confirm };
    httpd_uri_t uri_ota_rollback = { .uri = "/api/ota/rollback", .method = HTTP_POST, .handler = handle_ota_rollback };
    httpd_uri_t uri_reboot = { .uri = "/api/system/reboot", .method = HTTP_POST, .handler = handle_reboot };

    httpd_register_uri_handler(server, &uri_root);
    httpd_register_uri_handler(server, &uri_status);
    httpd_register_uri_handler(server, &uri_processes);
    httpd_register_uri_handler(server, &uri_telemetry);
    httpd_register_uri_handler(server, &uri_process_history);
    httpd_register_uri_handler(server, &uri_ota);
    httpd_register_uri_handler(server, &uri_process_action);
    httpd_register_uri_handler(server, &uri_command);
    httpd_register_uri_handler(server, &uri_ota_confirm);
    httpd_register_uri_handler(server, &uri_ota_rollback);
    httpd_register_uri_handler(server, &uri_reboot);

    OS_LOGI(TAG, "Web portal started");
    return ESP_OK;
}

void webportal_stop(void)
{
    if (!server) return;
    httpd_stop(server);
    server = NULL;
}

#else
esp_err_t webportal_start(void) { return ESP_ERR_NOT_SUPPORTED; }
void webportal_stop(void) { }
#endif
