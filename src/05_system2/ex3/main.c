// Tache1.c
int main() {
    volatile long long sum = 0; // déclare 'sum' comme volatile pour empêcher l'optimisation du compilateur
    for (;;) {
        sum++;
    }
    return 0;
}
