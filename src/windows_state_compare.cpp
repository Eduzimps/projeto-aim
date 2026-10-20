#include "aim/windows_probe_support.hpp"
#include "aim/state_comparison.hpp"
#include <iostream>

namespace wp = aim::windows_probe;
namespace sp = aim::scene_profile;
namespace sc = aim::state_comparison;
namespace cp = aim::camera_profile;
namespace {
bool game_foreground(DWORD pid) {
    DWORD foreground = 0;
    GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    return foreground == pid;
}
struct Observation { sc::Sample sample; std::uint64_t selected = 0; };
Observation observe(wp::Session& session, std::uint64_t previous, bool select_new) {
    wp::MemoryReader memory{session.process.value,0};
    const auto start = GetTickCount64();
    const cp::ReadBytes reader = [&](auto address, void* destination, auto bytes) {
        return GetTickCount64()-start < 2000 && memory(address,destination,bytes);
    };
    auto scene = sp::probe(session.base,session.mapped_size,reader);
    Observation observation;
    observation.selected = previous;
    if (select_new) {
        if (!aim::calibration::refresh_camera(scene,session.base,session.mapped_size,reader)) {
            observation.sample.status = "camera_refresh_failed"; return observation;
        }
        observation.selected = sc::select(scene);
    }
    observation.sample = sc::capture(scene,session.base,observation.selected,reader);
    if (GetTickCount64()-start >= 2000) {
        observation.sample = {}; observation.sample.status = "capture_timeout";
    }
    return observation;
}
}
int main() {
    SetConsoleOutputCP(CP_UTF8);
    wp::Session session; wp::IdentityReport identity;
    if (!wp::open_client(session,identity)) {
        std::cerr << "Não foi possível abrir a versão analisada do jogo: " << identity.status << '\n';
        return 1;
    }
    std::cout << "ZYON · comparação de três fases (somente leitura)\n"
        "Volte ao treinamento. Aponte o centro da tela para os PÉS de um único boneco.\n"
        "F6: vivo / iniciar novamente. F7: morto, seguindo a mesma referência.\n"
        "F8: reaparecido; aponte novamente para os pés do mesmo boneco.\n"
        "Uma captura aceita emite um sinal curto. Se falhar, ajuste a mira e repita.\n"
        "F8 salva e encerra; END salva o que houver. Prazo máximo: 5 minutos.\n"
        "As fases são marcadas por você; a ferramenta ainda não conhece vida ou morte.\n" << std::flush;
    const auto started = GetTickCount64();
    const DWORD pid = GetProcessId(session.process.value);
    std::array<sc::Sample,3> samples{};
    std::array<bool,3> down{{true,true,true}};
    std::uint64_t selected = 0;
    bool done = false;
    while (!done && GetTickCount64()-started < 300000) {
        if (GetAsyncKeyState(VK_END)&0x8000) break;
        DWORD exit_code=0;
        if (!GetExitCodeProcess(session.process.value,&exit_code) || exit_code != STILL_ACTIVE) break;
        constexpr std::array<int,3> keys{{VK_F6,VK_F7,VK_F8}};
        for (std::size_t phase=0;phase<keys.size();++phase) {
            const bool pressed = (GetAsyncKeyState(keys[phase])&0x8000) != 0;
            const bool edge = pressed && !down[phase]; down[phase] = pressed;
            if (!edge || !game_foreground(pid)) continue;
            if ((phase && !sc::captured(samples[0])) ||
                (phase == 2 && std::strcmp(samples[1].status,"not_captured") == 0)) {
                std::cout << "Capture F6, depois F7 e por último F8.\n" << std::flush;
                MessageBeep(MB_ICONEXCLAMATION); continue;
            }
            auto observation = observe(session,selected,phase != 1);
            const bool absent = std::strncmp(observation.sample.status,"not_observed_",13) == 0;
            std::cout << "F" << (phase+6) << ": " << observation.sample.status << '\n' << std::flush;
            if (!sc::captured(observation.sample) && !(phase == 1 && absent)) {
                MessageBeep(MB_ICONEXCLAMATION); continue;
            }
            if (phase == 0) { samples = {}; selected = observation.selected; }
            samples[phase] = observation.sample;
            Beep(880,90);
            if (phase == 2) { done = true; break; }
        }
        Sleep(20);
    }
    const auto json = sc::json_report(samples);
    DWORD error = 0;
    if (!wp::save_report(json,L"BloodStrike-comparacao",error)) {
        std::cerr << "Falha ao salvar na Área de Trabalho; erro " << error << ".\n";
        std::cout << json; return 1;
    }
    std::cout << "Salvo na Área de Trabalho: BloodStrike-comparacao-*.json\n" << json;
    return 0;
}
