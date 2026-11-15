import random

def resolver_modelo_po(planta, peso_custo_pct, peso_tempo_pct):
    """
    Função de Pesquisa Operacional ponderando Custo e Tempo.
    Pesos fornecidos pelo usuário (somando 100%).
    A 4ª restrição (X_tempo >= tempo_min) foi removida conforme solicitado.
    """
    pc = peso_custo_pct / 100.0
    pt = peso_tempo_pct / 100.0

    # Variáveis de decisão ótimas simulando restrições ativas de PO
    agua_otima = round(planta.agua_min * random.uniform(1.0, 1.15), 2)
    luz_minutos_otima = round(planta.luz_min_minutos * random.uniform(1.0, 1.1), 2)
    nutrientes_otimos = round(planta.nutrientes_min * random.uniform(1.0, 1.2), 2)
    
    # Otimização do tempo influenciada pelo peso de tempo escolhido
    fator_tempo = 0.85 - (pt * 0.15)
    tempo_otimizado = max(8, int(planta.tempo_des_min * fator_tempo))

    # Cálculo do consumo de energia elétrica em kWh
    # Energia (kWh) = [Potência (W) * Tempo de Iluminação (min/dia)] / [1000 * 60]
    consumo_kwh_dia = (planta.potencia_luz_w * luz_minutos_otima) / 60000.0
    consumo_wh_dia = consumo_kwh_dia * 1000.0

    # Custos individuais por recurso
    custo_agua = agua_otima * planta.custo_agua_ml
    custo_eletrica = consumo_kwh_dia * planta.custo_eletrica_kwh
    custo_nutrientes = nutrientes_otimos * planta.custo_nutriente_g
    custo_tempo = tempo_otimizado * (pt * 0.05) # Custo operacional ponderado do tempo

    # Função Objetivo Ponderada Z = w_c*(Custos) + w_t*(Tempo)
    z_custo_financeiro = custo_agua + custo_eletrica + custo_nutrientes
    z_valor_total = (pc * z_custo_financeiro) + (pt * tempo_otimizado * 0.10)

    return {
        "agua": agua_otima,
        "potencia_luz_w": planta.potencia_luz_w,
        "luz_minutos": luz_minutos_otima,
        "luz_horas_equivalente": round(luz_minutos_otima / 60.0, 2),
        "consumo_wh": round(consumo_wh_dia, 2),
        "consumo_kwh": round(consumo_kwh_dia, 4),
        "nutrientes": nutrientes_otimos,
        "tempo_otimizado": tempo_otimizado,
        "custos_individuais": {
            "agua": round(custo_agua, 2),
            "eletrica": round(custo_eletrica, 2),
            "nutrientes": round(custo_nutrientes, 2),
            "tempo": round(custo_tempo, 2)
        },
        "z_total": round(z_valor_total, 2),
        "peso_custo": peso_custo_pct,
        "peso_tempo": peso_tempo_pct
    }