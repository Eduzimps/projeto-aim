# Zyon — laboratório visual para Windows

**Estado atual: menu e prévia independente. Ainda não é um cheat funcional para Blood Strike.**

A aplicação é nativa em C++17/Win32. O menu grafite/violeta permite experimentar caixas com barra de vida lateral, círculo de FOV, seleção de cabeça/tronco e suavização configurável. A cena contém somente alvos sintéticos. O botão direito movimenta um retículo desenhado na própria janela; o programa não controla o mouse do Windows.

## Executar

No GitHub, clique em **Code → Download ZIP**, extraia o arquivo baixado e entre na pasta `projeto-aim-main`. Depois abra `dist/zyon-preview.exe` com dois cliques. Também é possível baixar somente o executável pela página dele em `dist/`, usando **Download raw file**.

No Windows x64, abra `dist/zyon-preview.exe`. O executável é independente de runtimes C++ adicionais.

O diagnóstico separado `dist/zyon-camera-probe.exe` verifica a cadeia candidata de câmera para a build analisada. Com o jogo aberto, ele produz `BloodStrike-camera-*.json` na Área de Trabalho. A análise estática e suas limitações estão em [docs/client-analysis.md](docs/client-analysis.md). O diagnóstico ainda não fornece ESP ou aimbot.

- **Mira / Visual:** alterna entre as opções.
- **FOV:** raio de 60 a 280 unidades visuais, escaladas com o DPI do Windows; não são graus de câmera.
- **Força:** 1–100%; menor significa convergência mais lenta.
- **Botão direito dentro da cena:** demonstra a suavização no retículo interno.
- **Insert:** mostra/oculta o painel; **1 / 2:** troca as abas.
- **Tab / Shift+Tab, setas e Espaço:** navega e ajusta os controles.
- **Esc:** interrompe a interação atual.

As preferências são salvas em `%LOCALAPPDATA%\Zyon\settings.ini`, na seção `ZyonPreview`. O indicador no rodapé informa se o salvamento funcionou. “Restaurar padrões” altera somente as opções desta prévia.

## Compilar e testar no ambiente de nuvem

O ambiente atual é Linux Debian 13. A preparação baixa pacotes MinGW do repositório oficial Debian por HTTPS, com verificação de assinatura e checksums pelo APT, e os extrai em `/workspace/.tools/zyon-mingw`. Não exige root nem altera o sistema.

```bash
cd /workspace/projeto-aim
bash scripts/setup-cloud.sh
```

Após a preparação:

```bash
bash scripts/test.sh
bash scripts/build-windows.sh
bash scripts/build-camera-probe.sh
```

Os comandos executam os testes portáteis e geram `dist/zyon-preview.exe` e `dist/zyon-camera-probe.exe` com MinGW x64 e avisos tratados como erros. `build/` e os arquivos temporários de `dist/` são ignorados pelo Git; os executáveis, a imagem da interface e o ZIP de distribuição são incluídos para download. `ZYON_MINGW_ROOT` permite escolher outro diretório do compilador e `CXX_WINDOWS` permite indicar um compilador MinGW já instalado.

Para abrir a prévia internamente na nuvem ou testar a interface com Wine/Xvfb:

```bash
bash scripts/setup-preview.sh
bash scripts/test-ui-cloud.sh
bash scripts/start-preview-cloud.sh
```

O teste de interface usa um prefixo Wine temporário e display separados, sem alterar as preferências de uma prévia já aberta. Verifica controles, salvamento, reabertura e redimensionamento. O último comando mantém a janela aberta em um display virtual; não publica uma página ou link de acesso. Os processos precisam ser iniciados novamente em tarefas futuras.

Validação desta entrega: compilação x64 concluída, suíte matemática aprovada e **23 verificações de interface aprovadas no Wine 10 com Xvfb a 96 DPI**. A preparação e a inicialização pelos scripts acima também foram exercitadas. A execução nativa em Windows e o comportamento dentro de Blood Strike não foram verificados.

## O que foi implementado

- Interface e persistência das opções no Windows.
- Prévia animada, caixas, barras de vida e FOV configurável.
- Núcleo independente em `include/aim/core.hpp`: projeção de coordenadas, geometria das caixas, normalização de vida e seleção de alvo por FOV circular.
- Suavização exponencial com força, limites de velocidade/aceleração e retenção de alvo. É um modelo matemático; não reproduz trajetórias humanas reais. O limite de aceleração admite frenagem imediata para impedir ultrapassagem do alvo ou respeitar uma redução do limite de velocidade.
- Testes de projeção, entradas inválidas, FOV, filtros, vida, limites e equivalência de convergência a 60/144 FPS quando os limites não estão ativos.

## O que impede a integração com Blood Strike

Os anexos `bloodstrike-dump.md` e `bloodstrike-offsets.json` contêm a mesma lista de offsets sem versão, módulo ou validação. Nomes como `BaseNetworkable`, `BasePlayer`, `PlayerEyes` e `TOD_Sky` lembram estruturas de Rust/Unity; não estabelecem compatibilidade com Blood Strike.

O executável recebido depois foi reconstruído com SHA256 verificado e contém interfaces Messiah/Python. Foi derivada uma cadeia candidata de câmera específica desse arquivo. Sua leitura em execução ainda precisa ser validada; os offsets antigos não foram reutilizados. Veja o relatório de análise acima.

Para implementar e verificar uma conexão real, ainda faltam a versão/build correta do jogo e dados correspondentes: módulo e layout da lista de entidades, identificação do jogador local/equipes, posições globais dos jogadores/ossos, tipos e origem dos valores de vida e matriz de câmera com suas convenções. Os endereços enviados não foram usados nem substituídos por palpites.

A prévia visual não se conecta ao processo do jogo. O diagnóstico separado faz leituras limitadas da câmera candidata. A compilação e os testes matemáticos não validam ESP ou aimbot dentro de Blood Strike.

### Coletar o cliente instalado para análise estática

`scripts/collect-client.ps1` é um auxiliar para Windows PowerShell 5.1 ou posterior. Com o jogo aberto, ele localiza o arquivo `BloodStrike.exe` no disco e copia seus bytes para ZIPs com até 30 MiB de dados originais por parte, acompanhados de `manifesto.json` com hashes SHA256. A saída fica em uma pasta nova `BloodStrike-analise-*` na Área de Trabalho.

O coletor lê somente o executável em disco, preserva os arquivos do jogo e não envia os ZIPs automaticamente. Os ZIPs e o manifesto podem ser anexados na conversa para investigar o cliente. A cópia do executável não garante que será possível identificar todos os dados de câmera e jogadores ou concluir a integração.

O script aceita `-ExecutablePath` para indicar o arquivo explicitamente e `-OutputDirectory` para escolher outra pasta existente. Falhas de acesso são reportadas; o coletor não solicita privilégios elevados. As pastas de coleta são ignoradas pelo Git e não devem ser publicadas no repositório.

## Organização

```text
include/aim/core.hpp     Matemática independente do sistema operacional
src/windows_app.cpp     Interface, configurações e cena sintética
tests/core_test.cpp      Verificações do núcleo
scripts/                Preparação, testes e compilação
```

Use o checkout existente em cada tarefa na nuvem. As tarefas já são isoladas; não é necessário criar um Git worktree.
