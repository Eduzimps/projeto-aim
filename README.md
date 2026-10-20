# Zyon — laboratório visual para Windows

**Estado atual: menu e prévia independente. Ainda não é um cheat funcional para Blood Strike.**

A aplicação é nativa em C++17/Win32. O menu grafite/violeta permite experimentar caixas com barra de vida lateral, círculo de FOV, seleção de cabeça/tronco e suavização configurável. A cena contém somente alvos sintéticos. O botão direito movimenta um retículo desenhado na própria janela; o programa não controla o mouse do Windows.

## Executar

No GitHub, clique em **Code → Download ZIP**, extraia o arquivo baixado e entre na pasta `projeto-aim-main`. Depois abra `dist/zyon-preview.exe` com dois cliques. Também é possível baixar somente o executável pela página dele em `dist/`, usando **Download raw file**.

No Windows x64, abra `dist/zyon-preview.exe`. O executável é independente de runtimes C++ adicionais.

O diagnóstico separado `dist/zyon-camera-probe.exe` verifica a cadeia candidata de câmera para a build analisada. Com o jogo aberto, ele produz `BloodStrike-camera-*.json` na Área de Trabalho. A análise estática e suas limitações estão em [docs/client-analysis.md](docs/client-analysis.md). O diagnóstico ainda não fornece ESP ou aimbot.

`dist/zyon-scene-probe.exe` verifica o mundo ativo do cenário, independentemente do campo `World` da câmera. Examina o nível padrão e os níveis ativos, suas áreas raiz e subáreas imediatas. Os limites globais são 16 níveis, 32 áreas, 256 nós de entidades, 4.096 leituras e 64 KiB solicitados; novas leituras param após uma janela de 2 segundos. Salva `BloodStrike-scene-*.json` na Área de Trabalho. O formato v4 inclui contagens, referência ao personagem local, presença de controlador de movimento e identificadores relativos dos recursos de esqueleto, além de até 16 amostras de posição/projeção. Reconhece também o tipo derivado `ActorComponent` observado em execução e distingue recurso de modelo de grafo de animação. Esses campos ainda não identificam inimigos, equipes ou vida. Execute dentro do treinamento; limites atingidos, listas vazias ou alterações durante a leitura são informados no relatório.

`dist/zyon-calibration.exe` é um teste visual de leitura: mostra pontos roxos nas origens dos candidatos com ator/esqueleto/controlador, excluindo a referência local. Abra o treinamento em modo janela ou janela sem bordas, execute o arquivo e volte ao jogo. **Insert** oculta/mostra os pontos; **End** encerra. O programa fecha sozinho após 120 segundos e se esconde quando o jogo perde o foco. Ele não controla entrada, desenha caixas ou mostra vida. A identificação de inimigos e equipes continua pendente. O objetivo é conferir se a projeção acompanha os bonecos na imagem real.

`dist/zyon-state-compare.exe` prepara uma comparação exploratória de três fases no treinamento. Volte ao jogo, aponte o centro da tela para os pés de um único boneco e pressione **F6** enquanto ele está vivo. Mate esse boneco e pressione **F7**; essa fase segue a referência escolhida em F6. Quando reaparecer, aponte novamente para seus pés e pressione **F8**. Um sinal curto confirma a captura; uma seleção ambígua ou leitura inválida permite repetir a tecla. F6 reinicia as fases. F8 salva `BloodStrike-comparacao-*.json` na Área de Trabalho e encerra; End ou o prazo de cinco minutos salvam o que houver. As teclas F6–F8 só são observadas enquanto o jogo está em primeiro plano. O JSON compara um prefixo nativo limitado, sem emitir endereços remotos, e não interpreta as diferenças como vida. Os rótulos vivo/morto/reaparecido são fornecidos pelo usuário. Se as referências mudarem ou uma fase estiver ausente, não compara os campos como se fossem o mesmo objeto.

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
bash scripts/build-scene-probe.sh
bash scripts/build-calibration.sh
bash scripts/build-state-compare.sh
```

Os comandos executam os testes portáteis e geram os executáveis de `dist/` com MinGW x64 e avisos tratados como erros. `build/` e os arquivos temporários de `dist/` são ignorados pelo Git; os executáveis, a imagem da interface e o ZIP de distribuição são incluídos para download. `ZYON_MINGW_ROOT` permite escolher outro diretório do compilador e `CXX_WINDOWS` permite indicar um compilador MinGW já instalado.

Para abrir a prévia internamente na nuvem ou testar a interface com Wine/Xvfb:

```bash
bash scripts/setup-preview.sh
bash scripts/test-ui-cloud.sh
bash scripts/test-probes-cloud.sh
bash scripts/start-preview-cloud.sh
```

O teste de interface usa um prefixo Wine temporário e display separados, sem alterar as preferências de uma prévia já aberta. Verifica controles, salvamento, reabertura e redimensionamento. O último comando mantém a janela aberta em um display virtual; não publica uma página ou link de acesso. Os processos precisam ser iniciados novamente em tarefas futuras.

Validação da prévia: compilação x64 concluída, suíte matemática aprovada e **23 verificações de interface aprovadas no Wine 10 com Xvfb a 96 DPI**. O teste separado dos diagnósticos verifica ausência do jogo, rejeição de outro executável e correspondência entre JSON do terminal e da Área de Trabalho. O primeiro teste real de cena mostrou `camera.World` nulo. O segundo acessou o mundo ativo, leu posições de 256 objetos e encontrou 35 com esqueleto, parando no limite de coleta. O terceiro encontrou a referência local e seis amostras não locais com esqueleto/controlador; os recursos dos modelos ficaram vazios ou bloqueados por um tipo derivado, agora reconhecido como `ActorComponent`. O relatório v4 confirmou a leitura desse tipo, mas seus campos de modelo e grafo vieram vazios. O usuário confirmou o alinhamento dos pontos após a redução do intervalo de atualização e relatou que o ponto continua presente após a morte do boneco. A identificação de inimigos, equipes e vida continua pendente.

## O que foi implementado

- Interface e persistência das opções no Windows.
- Prévia animada, caixas, barras de vida e FOV configurável.
- Núcleo independente em `include/aim/core.hpp`: projeção de coordenadas, geometria das caixas, normalização de vida e seleção de alvo por FOV circular.
- Suavização exponencial com força, limites de velocidade/aceleração e retenção de alvo. É um modelo matemático; não reproduz trajetórias humanas reais. O limite de aceleração admite frenagem imediata para impedir ultrapassagem do alvo ou respeitar uma redução do limite de velocidade.
- Testes de projeção, entradas inválidas, FOV, filtros, vida, limites e equivalência de convergência a 60/144 FPS quando os limites não estão ativos.

## O que impede a integração com Blood Strike

Os anexos `bloodstrike-dump.md` e `bloodstrike-offsets.json` contêm a mesma lista de offsets sem versão, módulo ou validação. Nomes como `BaseNetworkable`, `BasePlayer`, `PlayerEyes` e `TOD_Sky` lembram estruturas de Rust/Unity; não estabelecem compatibilidade com Blood Strike.

O executável recebido depois foi reconstruído com SHA256 verificado e contém interfaces Messiah/Python. A cadeia de câmera derivada desse arquivo produziu três amostras coerentes no teste enviado pelo usuário. Os offsets antigos não foram reutilizados. Veja o relatório de análise acima.

A enumeração parcial de cena e o alinhamento visual dos pontos foram observados em execução. Ainda faltam identificar com segurança quais objetos representam jogadores, separar equipes, obter ossos e confirmar os tipos e a origem dos valores de vida. A presença de um ponto não estabelece que o boneco esteja vivo. Os endereços enviados não foram substituídos por palpites.

A prévia visual não se conecta ao processo do jogo. Os diagnósticos separados fazem leituras limitadas, com verificação da build e sem alterar memória ou controlar entrada. A compilação e os testes matemáticos não validam ESP ou aimbot dentro de Blood Strike.

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
