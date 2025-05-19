# 🌊 HydroGuard - Estação Inteligente de Monitoramento de Cheias

Projeto desenvolvido para simular uma estação de monitoramento de cheias utilizando a placa BitDog Lab com microcontrolador RP2040, baseado em FreeRTOS.

---

## 📌 Objetivo

O objetivo principal é demonstrar o uso de tarefas e filas no FreeRTOS para implementar um sistema reativo e confiável, capaz de:

- Monitorar níveis de água e volume de chuva (simulados via joystick analógico)
- Emitir alertas visuais com LED RGB e matriz de LEDs 5x5
- Emitir alertas sonoros com buzzer
- Exibir dados em tempo real no display OLED
- Operar com diferentes modos de alerta baseados na gravidade da situação

---

## 🧠 Funcionalidades

- **Modo Normal**: operação segura com atualizações visuais leves
- **Modo Atenção**: alerta inicial com LED amarelo e bipe ocasional
- **Modo Alerta**: LED vermelho e avisos sonoros frequentes
- **Modo Crítico**: emergência com sirene e exibição de “EVACUAÇÃO IMEDIATA”

---

## ⚙️ Arquitetura do Sistema

O sistema é dividido em 6 tarefas do FreeRTOS:

| Tarefa              | Função Principal                          |
|---------------------|-------------------------------------------|
| `vSensorTask`       | Leitura do joystick (nível/chuva)         |
| `vProcessingTask`   | Lógica de decisão e gerenciamento de modo |
| `vDisplayTask`      | Exibição de dados no display OLED         |
| `vLedRGBTask`       | Controle do LED RGB via PWM               |
| `vMatrixLedTask`    | Padrões visuais na matriz LED 5x5         |
| `vBuzzerTask`       | Emissão de sons com buzzer PWM            |

A comunicação entre as tarefas é **100% baseada em filas** (queues), sem uso de semáforos ou mutexes.

---

## 🔌 Componentes Utilizados

| Componente      | Pinos GPIO       | Função                             |
|------------------|------------------|------------------------------------|
| Joystick         | GPIO26, GPIO27   | Simula níveis de água e chuva      |
| LED RGB          | GPIO11,12,13     | Indica modo do sistema             |
| Matriz LED 5x5   | GPIO7            | Exibe padrões visuais              |
| Buzzer PWM       | GPIO10           | Gera sons de alerta                |
| Display OLED     | GPIO14, GPIO15   | Mostra dados e status              |
| Botão B          | GPIO6            | Entrada para BOOTSEL               |

---

## 📦 Requisitos para Compilação

Para compilar este projeto, é necessário baixar o núcleo do FreeRTOS manualmente. Siga os passos abaixo:

1. Clone o repositório do FreeRTOS Kernel:
   ```bash
   git clone https://github.com/FreeRTOS/FreeRTOS-Kernel.git
   ```

2. No arquivo `CMakeLists.txt` do seu projeto, ajuste o caminho da variável `FREERTOS_KERNEL_PATH` de acordo com o local onde você salvou a pasta clonada. Exemplo:

   ```cmake
   set(FREERTOS_KERNEL_PATH "Z:/FreeRTOS-Kernel") 
   include(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake)
   ```

3. Certifique-se de que o arquivo `FreeRTOSConfig.h` está localizado dentro da pasta `include/` no seu projeto.

Esses ajustes garantem que o FreeRTOS seja corretamente integrado ao ambiente de compilação para o RP2040.

---