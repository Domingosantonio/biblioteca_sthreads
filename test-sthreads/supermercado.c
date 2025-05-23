#include <stdio.h>
#include <stdlib.h>
#include "/home/domingos/Transferências/sthreads/sthread_lib/sthread_user.h"
#include "sthread.h"


#define NUM_CAIXAS 3  // Número de caixas no supermercado

// Estrutura para representar um caixa do supermercado
typedef struct {
    sthread_mon_t mon;      // Monitor para controle de acesso
    queue_t* fila_clientes; // Fila de clientes específica deste caixa
    int num_clientes;       // Número de clientes na fila
    int id;                 // Identificador do caixa
} caixa_t;

// Variáveis globais
static caixa_t caixas[NUM_CAIXAS];
static sthread_mon_t supermercado_mon; // Monitor global para controle

// Inicialização dos caixas
void init_caixas() {
    supermercado_mon = sthread_monitor_init();
    for (int i = 0; i < NUM_CAIXAS; i++) {
        caixas[i].mon = sthread_monitor_init();
        caixas[i].fila_clientes = queue_create();
        caixas[i].num_clientes = 0;
        caixas[i].id = i;
    }
}

// Liberar recursos dos caixas
void free_caixas() {
    for (int i = 0; i < NUM_CAIXAS; i++) {
        queue_delete(caixas[i].fila_clientes);
        sthread_monitor_free(caixas[i].mon);
    }
    sthread_monitor_free(supermercado_mon);
}

// Função do funcionário (caixa)
void* funcionario(void *arg) {
    caixa_t *meu_caixa = (caixa_t *)arg;
    int id = meu_caixa->id;
    
    while (1) {
        sthread_monitor_enter(meu_caixa->mon);
        
        // Se não há clientes na minha fila, verifica outros caixas
        if (queue_isempty(meu_caixa->fila_clientes)) {
            sthread_monitor_enter(supermercado_mon);
            
            // Procura em outros caixas por clientes
            for (int i = 0; i < NUM_CAIXAS; i++) {
                if (i != id && !queue_isempty(caixas[i].fila_clientes)) {
                    // Move um cliente de outra fila para a minha
                    sthread_t cliente = queue_remove(caixas[i].fila_clientes);
                    caixas[i].num_clientes--;
                    queue_insert(meu_caixa->fila_clientes, cliente);
                    meu_caixa->num_clientes++;
                    break;
                }
            }
            
            sthread_monitor_exit(supermercado_mon);
        }
        
        // Se ainda não há clientes, espera
        if (queue_isempty(meu_caixa->fila_clientes)) {
            sthread_monitor_wait(meu_caixa->mon);
        } else {
            // Atende o próximo cliente
            sthread_t cliente = queue_remove(meu_caixa->fila_clientes);
            meu_caixa->num_clientes--;
            
            sthread_monitor_exit(meu_caixa->mon);
            
            // Simula o tempo de atendimento
            printf("Caixa %d atendendo cliente %d\n", id, cliente->tid);
            sthread_sleep(100); // Tempo de atendimento
            
            // Notifica outros caixas que podem ter clientes
            sthread_monitor_enter(supermercado_mon);
            for (int i = 0; i < NUM_CAIXAS; i++) {
                if (!queue_isempty(caixas[i].fila_clientes)) {
                    sthread_monitor_enter(caixas[i].mon);
                    sthread_monitor_signal(caixas[i].mon);
                    sthread_monitor_exit(caixas[i].mon);
                }
            }
            sthread_monitor_exit(supermercado_mon);
        }
    }
    return NULL;
}

// Função do cliente
void* cliente(void *arg) {
    (void)arg; // Argumento não usado
    
    // Encontra a fila mais curta
    int menor_fila = 0;
    int menor_num = caixas[0].num_clientes;
    
    sthread_monitor_enter(supermercado_mon);
    
    for (int i = 1; i < NUM_CAIXAS; i++) {
        if (caixas[i].num_clientes < menor_num) {
            menor_num = caixas[i].num_clientes;
            menor_fila = i;
        }
    }
    
    // Entra na fila do caixa escolhido
    sthread_monitor_enter(caixas[menor_fila].mon);
    queue_insert(caixas[menor_fila].fila_clientes, sthread_self());
    caixas[menor_fila].num_clientes++;
    
    // Notifica o caixa que há um novo cliente
    sthread_monitor_signal(caixas[menor_fila].mon);
    
    sthread_monitor_exit(caixas[menor_fila].mon);
    sthread_monitor_exit(supermercado_mon);
    
    // Espera ser atendido (a thread será movida quando atendida)
    return NULL;
}

// Função principal para teste
int main() {
    sthread_init();
    init_caixas();
    
    // Cria threads para os funcionários (caixas)
    for (int i = 0; i < NUM_CAIXAS; i++) {
        sthread_create(funcionario, &caixas[i]);
    }
    
    // Cria threads para os clientes
    for (int i = 0; i < 10; i++) {
        sthread_sleep(50); // Intervalo entre chegada de clientes
        sthread_create(cliente, NULL);
    }
    
    // Espera um tempo para simulação
    sthread_sleep(5000);
    
    free_caixas();
    return 0;
}
