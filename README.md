# 🚦 Semáforo Inteligente - Grupo 1 T18

## Projeto: Semáforo Inteligente com Modo Noturno e Interface Web  
Este projeto implementa dois semáforos inteligentes controlados por um **ESP32**, utilizando um **sensor LDR** para detectar luminosidade ambiente e adaptar automaticamente o comportamento dos sinais, incluindo um **modo noturno** e detecção de veículos.  

## Integrantes

- Miguel Ferreira de Siqueira Almeida
- Lucas Picinato Rogero
- Bruno Frossard Silva
- Enzo Araujo de Rezendo
- Filipe Sudbrack Nunes
- Carlos Icaro Kauã Coelho Paiva

---

## Materiais Utilizados
| Componente | Quantidade |
|------------|------------|
| ESP32 | 1 |
| Protoboard | 1 |
| LED Vermelho | 2 |
| LED Amarelo | 2 |
| LED Verde | 2 |
| LDR | 1 |
| Resistor 10kΩ | 1 |
| Resistores 220Ω (LEDs) | 6 |
| Jumpers | Vários |
| Cabo USB | 1 |

---

## Montagem do Circuito

### Pinos dos Semáforos

#### Semáforo 1:
| LED | GPIO |
|-----|------|
| Verde | 21 |
| Amarelo | 22 |
| Vermelho | 23 |

#### Semáforo 2:
| LED | GPIO |
|-----|------|
| Verde | 5 |
| Amarelo | 18 |
| Vermelho | 19 |

---

## Funcionamento do Sistema

### Modo Normal

Os semáforos funcionam com um ciclo padrão:

Semáforo 1  
- Verde: 6 segundos  
- Amarelo: 2 segundos  
- Vermelho: 6 segundos  

Semáforo 2  
- Vermelho enquanto o semáforo 1 está verde  
- Verde enquanto o semáforo 1 está vermelho 

---

### Modo Noturno

Ativado automaticamente quando o valor do LDR é baixo (indicando ambiente escuro).

Comportamento:
- LEDs amarelos piscando em ambos os semáforos

O modo noturno também pode ser ativado manualmente pela interface web.

---

### Funcionamento do LDR

O **LDR** (Light Dependent Resistor) é um sensor resistivo cuja resistência varia de acordo com a quantidade de luz incidente. Em ambientes claros, sua resistência diminui; em ambientes escuros, aumenta. No projeto, ele é utilizado para:

1. Detectar condições de iluminação do ambiente (dia/noite)  
2. Identificar variações rápidas de luminosidade que simulam a passagem de um veículo  

O **LDR** foi conectado como um **divisor de tensão**, o que permite ao ESP32 ler valores analógicos entre **0** e **4095**.

#### Montagem Eletrônica

**3.3V ---- LDR ---- (GPIO 34 - leitura analógica) ---- Resistor 10k ---- GND**

#### Leitura do LDR

O ESP32 converte a tensão em um valor entre:

- 0 (escuro total)
- 4095 (muita luz)

#### Intrepretação dos Valores:

| Condição | Valor do LDR |
| -------- | ------------ |
| Ambiente Claro | Acima de 2000 |
| Ambiente Moderado | 1200 a 2000 |
| Ambiente escuro / noite | Abaixo de 1200 |

Ativação Automática Modo Noturno

```cpp

```

#### Limitações do LDR

- É sensível à luz ambiente (janelas, lâmpadas, reflexos);
- Não mede distância, apenas intensidade de luz;
- Mudanças lentas de luminosidade podem ser confundidas com transição dia/noite;

---

### Interface Web

A interface permite:

- Visualizar o valor do LDR  
- Ativar ou desativar o modo noturno  
- Ajustar tempos dos semáforos  
- Ver o estado atual (verde/amarelo/vermelho)  

Tecnologias empregadas:
- HTML  
- CSS simples  
- JavaScript (requisições fetch)  
- WebServer do ESP32  

--- 

## Código completo utilizado

```cpp
xxxxxxxxxxxx
```

## Vídeo Demonstração
[Clique aqui](x)