
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX 100
#define MAX_TOTAL_BURST 100000

typedef struct {
    int pid, arrival, burst;
    int completion, turnaround, waiting;
} Process;

typedef struct {
    int pid;              /* 0 means CPU idle */
    int start, end;
} Segment;

typedef struct {
    char name[32];
    Process p[MAX];
    Segment *segments;
    int count, capacity;
    int n, quantum;
    int totalBurst, makespan;
} Result;

void initResult(Result *r, const char *name,
                Process input[], int n, int quantum) {
    memset(r, 0, sizeof(*r));
    snprintf(r->name, sizeof(r->name), "%s", name);
    r->n = n;
    r->quantum = quantum;

    for (int i = 0; i < n; i++) {
        r->p[i] = input[i];
        r->totalBurst += input[i].burst;
    }
}

void freeResult(Result *r) {
    free(r->segments);
    r->segments = NULL;
    r->count = r->capacity = 0;
}

int addSegment(Result *r, int pid, int start, int end) {
    if (end <= start)
        return 1;

    /* Combine consecutive intervals for the same process. */
    if (r->count > 0) {
        Segment *last = &r->segments[r->count - 1];

        if (last->pid == pid && last->end == start) {
            last->end = end;
            return 1;
        }
    }

    if (r->count == r->capacity) {
        int newCapacity = r->capacity ? r->capacity * 2 : 16;
        Segment *temp = realloc(
            r->segments, (size_t)newCapacity * sizeof(Segment));

        if (!temp) {
            fprintf(stderr, "Memory allocation failed.\n");
            return 0;
        }

        r->segments = temp;
        r->capacity = newCapacity;
    }

    r->segments[r->count].pid = pid;
    r->segments[r->count].start = start;
    r->segments[r->count].end = end;
    r->count++;
    return 1;
}

void calculateMetrics(Result *r) {
    r->makespan = 0;

    for (int i = 0; i < r->n; i++) {
        Process *p = &r->p[i];
        p->turnaround = p->completion - p->arrival;
        p->waiting = p->turnaround - p->burst;

        if (p->completion > r->makespan)
            r->makespan = p->completion;
    }
}

int scheduleFCFS(Result *r, Process input[], int n) {
    initResult(r, "FCFS", input, n, 0);

    int order[MAX];
    for (int i = 0; i < n; i++)
        order[i] = i;

    /* Stable ordering by arrival time. */
    for (int i = 1; i < n; i++) {
        int key = order[i];
        int j = i - 1;

        while (j >= 0 &&
               input[order[j]].arrival > input[key].arrival) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }

    int time = 0;

    for (int k = 0; k < n; k++) {
        int i = order[k];

        if (time < input[i].arrival) {
            if (!addSegment(r, 0, time, input[i].arrival))
                return 0;
            time = input[i].arrival;
        }

        int start = time;
        time += input[i].burst;

        if (!addSegment(r, input[i].pid, start, time))
            return 0;

        r->p[i].completion = time;
    }

    calculateMetrics(r);
    return 1;
}

int scheduleSJF(Result *r, Process input[], int n) {
    initResult(r, "SJF", input, n, 0);

    int done[MAX] = {0};
    int completed = 0, time = 0;

    while (completed < n) {
        int best = -1;

        for (int i = 0; i < n; i++) {
            if (done[i] || input[i].arrival > time)
                continue;

            if (best == -1 ||
                input[i].burst < input[best].burst ||
                (input[i].burst == input[best].burst &&
                 input[i].arrival < input[best].arrival)) {
                best = i;
            }
        }

        if (best == -1) {
            int next = -1;

            for (int i = 0; i < n; i++) {
                if (!done[i] &&
                    (next == -1 ||
                     input[i].arrival < input[next].arrival))
                    next = i;
            }

            if (next == -1)
                break;

            if (!addSegment(r, 0, time, input[next].arrival))
                return 0;

            time = input[next].arrival;
            continue;
        }

        int start = time;
        time += input[best].burst;

        if (!addSegment(r, input[best].pid, start, time))
            return 0;

        r->p[best].completion = time;
        done[best] = 1;
        completed++;
    }

    calculateMetrics(r);
    return 1;
}

int scheduleRR(Result *r, Process input[], int n, int quantum) {
    initResult(r, "Round Robin", input, n, quantum);

    int *queue = malloc(
        (size_t)(r->totalBurst + n + 10) * sizeof(int));

    if (!queue) {
        fprintf(stderr, "Queue allocation failed.\n");
        return 0;
    }

    int front = 0, rear = 0;
    int added[MAX] = {0};
    int remaining[MAX];
    int completed = 0, time = 0;

    for (int i = 0; i < n; i++)
        remaining[i] = input[i].burst;

    while (completed < n) {
        /* Add all processes that have arrived. */
        for (int i = 0; i < n; i++) {
            if (!added[i] && input[i].arrival <= time) {
                queue[rear++] = i;
                added[i] = 1;
            }
        }

        if (front == rear) {
            int next = -1;

            for (int i = 0; i < n; i++) {
                if (!added[i] &&
                    (next == -1 ||
                     input[i].arrival < input[next].arrival))
                    next = i;
            }

            if (next == -1)
                break;

            if (!addSegment(r, 0, time, input[next].arrival)) {
                free(queue);
                return 0;
            }

            time = input[next].arrival;
            continue;
        }

        int idx = queue[front++];
        int run = remaining[idx] < quantum
                    ? remaining[idx] : quantum;

        int start = time;
        time += run;
        remaining[idx] -= run;

        if (!addSegment(r, input[idx].pid, start, time)) {
            free(queue);
            return 0;
        }

        /*
         * Arrivals during the time slice join the queue
         * before the unfinished process is re-enqueued.
         */
        for (int i = 0; i < n; i++) {
            if (!added[i] && input[i].arrival <= time) {
                queue[rear++] = i;
                added[i] = 1;
            }
        }

        if (remaining[idx] > 0) {
            queue[rear++] = idx;
        } else {
            r->p[idx].completion = time;
            completed++;
        }
    }

    free(queue);
    calculateMetrics(r);
    return 1;
}

void printGantt(const Result *r) {
    printf("\nGantt Chart - %s\n", r->name);

    for (int i = 0; i < r->count; i++) {
        if (r->segments[i].pid == 0)
            printf("| IDLE ");
        else
            printf("| P%d ", r->segments[i].pid);
    }
    printf("|\n");

    if (r->count == 0)
        return;

    printf("%d", r->segments[0].start);

    for (int i = 0; i < r->count; i++)
        printf("\t%d", r->segments[i].end);

    printf("\n");
}

double averageWT(const Result *r) {
    double sum = 0;
    for (int i = 0; i < r->n; i++)
        sum += r->p[i].waiting;
    return sum / r->n;
}

double averageTAT(const Result *r) {
    double sum = 0;
    for (int i = 0; i < r->n; i++)
        sum += r->p[i].turnaround;
    return sum / r->n;
}

void displayResult(const Result *r) {
    printf("\n===== %s =====\n", r->name);
    printf("PID\tAT\tBT\tCT\tTAT\tWT\n");

    for (int i = 0; i < r->n; i++) {
        const Process *p = &r->p[i];

        printf("P%d\t%d\t%d\t%d\t%d\t%d\n",
               p->pid, p->arrival, p->burst,
               p->completion, p->turnaround, p->waiting);
    }

    printf("Average waiting time: %.2f\n", averageWT(r));
    printf("Average turnaround time: %.2f\n", averageTAT(r));

    if (r->makespan > 0) {
        printf("CPU utilization: %.2f%%\n",
            100.0 * r->totalBurst / r->makespan);

        printf("Throughput: %.4f processes/time-unit\n",
            (double)r->n / r->makespan);
    }

    printGantt(r);
}

void compareResults(const Result results[], int count) {
    printf("\n========== ALGORITHM COMPARISON ==========\n");
    printf("%-15s %12s %12s %14s %12s\n",
           "Algorithm", "Avg WT", "Avg TAT",
           "CPU Util(%)", "Throughput");

    for (int i = 0; i < count; i++) {
        const Result *r = &results[i];

        printf("%-15s %12.2f %12.2f %14.2f %12.4f\n",
               r->name, averageWT(r), averageTAT(r),
               r->makespan > 0
                   ? 100.0 * r->totalBurst / r->makespan : 0.0,
               r->makespan > 0
                   ? (double)r->n / r->makespan : 0.0);
    }

    int best = 0;
    for (int i = 1; i < count; i++) {
        if (averageWT(&results[i]) < averageWT(&results[best]))
            best = i;
    }

    printf("\nLowest average waiting time: %s (%.2f)\n",
           results[best].name, averageWT(&results[best]));
}

int exportCSV(const Result results[], int count) {
    FILE *file = fopen("results.csv", "w");

    if (!file) {
        perror("Could not create results.csv");
        return 0;
    }

    fprintf(file,
        "Algorithm,PID,Arrival,Burst,Completion,Turnaround,Waiting\n");

    for (int k = 0; k < count; k++) {
        for (int i = 0; i < results[k].n; i++) {
            const Process *p = &results[k].p[i];

            fprintf(file, "%s,%d,%d,%d,%d,%d,%d\n",
                    results[k].name, p->pid, p->arrival,
                    p->burst, p->completion,
                    p->turnaround, p->waiting);
        }
    }

    if (fclose(file) != 0) {
        perror("Error closing CSV file");
        return 0;
    }

    return 1;
}

int main(void) {
    Process input[MAX];
    Result results[3] = {0};
    int n, quantum;
    long long totalBurst = 0;

    printf("CPU Scheduling Algorithm Simulator\n");
    printf("1. FCFS\n2. Non-preemptive SJF\n3. Round Robin\n");
    printf("All algorithms will run for comparison.\n\n");

    printf("Enter number of processes (1-%d): ", MAX);

    if (scanf("%d", &n) != 1 || n < 1 || n > MAX) {
        fprintf(stderr, "Invalid process count.\n");
        return 1;
    }

    for (int i = 0; i < n; i++) {
        input[i].pid = i + 1;

        printf("Enter arrival time and burst time for P%d: ", i + 1);

        if (scanf("%d %d", &input[i].arrival,
                  &input[i].burst) != 2 ||
            input[i].arrival < 0 ||
            input[i].arrival > 10000000 ||
            input[i].burst <= 0) {
            fprintf(stderr, "Invalid arrival or burst time.\n");
            return 1;
        }

        totalBurst += input[i].burst;

        if (totalBurst > MAX_TOTAL_BURST) {
            fprintf(stderr,
                "Total burst time must not exceed %d.\n",
                MAX_TOTAL_BURST);
            return 1;
        }
    }

    printf("Enter Round Robin time quantum: ");

    if (scanf("%d", &quantum) != 1 ||
        quantum <= 0 || quantum > MAX_TOTAL_BURST) {
        fprintf(stderr, "Invalid time quantum.\n");
        return 1;
    }

    if (!scheduleFCFS(&results[0], input, n) ||
        !scheduleSJF(&results[1], input, n) ||
        !scheduleRR(&results[2], input, n, quantum)) {
        for (int i = 0; i < 3; i++)
            freeResult(&results[i]);
        return 1;
    }

    for (int i = 0; i < 3; i++)
        displayResult(&results[i]);

    compareResults(results, 3);

    if (exportCSV(results, 3))
        printf("\nResults exported to results.csv\n");

    for (int i = 0; i < 3; i++)
        freeResult(&results[i]);

    return 0;
}
