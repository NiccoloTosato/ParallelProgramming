#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>
#include <cblas.h>

#ifndef N
   #define N 21
#endif


void print_matrix(double* A,int n_col) {
  //debug purpose: print a matrix
  for(int i=0;i<n_col;++i){
    for(int j=0;j<N;++j)
      printf("%3.3g ",A[j+i*N]);
    printf("\n");
  }
}

void print_matrix_transpose(double* A,int n_col) {
  //debug purpose: print a matrix
  for(int i=0;i<N;++i){
    for(int j=0;j<n_col;++j)
      printf("%3.3g ",A[j+i*n_col]);
    printf("\n");
  }
}

void print_matrix_square(double* A,int n_col) {
  //debug purpose: print a matrix
  for(int i=0;i<n_col;++i){
    for(int j=0;j<n_col;++j)
      printf("%3.3g ",A[j+i*n_col]);
    printf("\n");
  }
}

void mat_mul(double* restrict A, double* restrict B, double* restrict C, int n_col,int n_fix) {

  for(int i=0; i<n_fix;++i) {
    int register row=i*N;
    for(int j=0;j<n_col;++j){
      int register idx=row+j;
      for(int k=0;k<N;++k) {
        C[idx]+=A[row+k]*B[k*n_col+j];
      }
    }
  }
}


void init_mat(double* A, int n_elem,int offset){
  //init the matrix sequentially
  for(int i=0;i<n_elem;++i)
    A[i]=i+offset;
}
void rank_mat(double* A, int n_elem,int rank){
  //init the matrix with the value of the rank
  for(int i=0;i<n_elem;++i)
    A[i]=rank;
}

int calculate_col(int tot_col,int procs,int rank) {
  //calculate how many row belong to the current rank
  return (rank < tot_col % procs) ? tot_col/procs +1 : tot_col/procs;
}

void set_recvcout(int* recvcount, int iter, int procs){
  //set the recv_count array 
  int current=calculate_col(N,procs,iter);
  for(int p=0;p<procs;++p){
    recvcount[p]=calculate_col(N,procs,p)*current;
  }

}

void set_displacement(int* displacement,const int* recvcount,int procs) {
  //calculate the displacement array using the recv_count array
  displacement[0]=0;
  for(int p=1;p<procs;++p)
    displacement[p]=displacement[p-1]+recvcount[p-1];
}

int calculate_offset(int procs,int tot_col,int iter) {
  //calculate the offset for each block
  int n_resto = (tot_col / procs) + 1;
  int n_normale = (tot_col / procs);
  int diff = iter - (tot_col % procs);
    return (iter < tot_col % procs) ? n_resto*iter  : n_resto * (tot_col % procs) + n_normale * diff ;
}


void extract(double* destination ,double*source,int n_fix,int n_col) {
  //linearize the block
  for(int line=0;line<n_fix;++line) {
    memcpy( destination+n_col*line,source+N*line, n_col*sizeof(double));
  }
}

int main(int argc,char* argv[]) {
  MPI_Init(&argc,&argv);
  int procs,rank;
  MPI_Comm_size(MPI_COMM_WORLD,&procs);
  MPI_Comm_rank(MPI_COMM_WORLD,&rank);

  //dimensione locale per il blocco relativo al rank, non cambiera mai, se sono uno dei primi mi becco il resto
  int n_fix=(rank < N % procs) ? N/procs +1 : N/procs;
  //dimensione del buffer, deve essere in grado di contenere il blocco piu grande che c'e' in giro
  int n_buffer= ( (N%procs) >= 1) ? (N/procs)+1 : N/procs ;
  //ad ogni iterazione verra' rivisto il numero di colonne
  int n_col;

  double* A=malloc(N*n_fix*sizeof(double));
  //memset(A, 0, N*n_col);
  rank_mat(A,n_fix*N,rank);

  double* B=malloc(N*n_fix*sizeof(double));
  //memset(B, 0, N*n_col);
  rank_mat(B,N*n_fix,rank); //ogni matrice ha il rank del possesore

  double* C=malloc(N*n_fix*sizeof(double));
  memset(C, 0, N*n_fix);

  //allocate the buffer, it use the larger n_col possible
  double* buffer=malloc(N*n_buffer*sizeof(double));
  //allocate the buffer to linearize the block
  double* square=malloc(n_buffer*n_buffer*sizeof(double));

  //allocate displacement ad recvout array
  int* displacement = malloc(procs*sizeof(int));
  int* recvcount = malloc(procs*sizeof(int));


  //MPI_Datatype blocco;

  for(int p=0;p<procs;++p){
    
    //numero di colonne all'iterazione corrente
    n_col=calculate_col(N,procs,p);
    set_recvcout(recvcount,p,procs);
    set_displacement(displacement,recvcount,procs);

    //MPI_Type_vector(n_buffer, n_buffer, N, MPI_DOUBLE, &blocco);
    //MPI_Type_commit(&blocco);

    int offset=calculate_offset(procs,N,p);

    
    //MPI_Type_size(blocco, &size);
    
    
    extract(square,B+offset,n_fix,n_col);

    MPI_Allgatherv( square , n_col*n_fix, MPI_DOUBLE,
                    buffer ,recvcount,displacement,MPI_DOUBLE,MPI_COMM_WORLD);
    
    //MPI_Type_free(&blocco);

    //mat_mul(A, buffer, C+offset, n_col, n_fix); 
    cblas_dgemm ( CblasRowMajor, CblasNoTrans, CblasNoTrans , n_fix , n_col , N , 1.0 , A , N , buffer , n_col , 0.0 ,  C+offset, N );

  }
  

#ifdef DEBUG
  double* C_final=malloc(N*N*sizeof(double));
  for(int p=0;p<procs;++p){
    recvcount[p]=N * calculate_col(N,procs,p);
  }
#endif  
  set_displacement(displacement,recvcount,procs);
  MPI_Gatherv(C,
             N*n_fix,
             MPI_DOUBLE,
             C_final,
              recvcount,displacement,
             MPI_DOUBLE,
             0,
             MPI_COMM_WORLD);
#if ( defined DEBUG2 || defined DEBUG)
  if(rank==0){
    printf("FINALEEEE \n");
    print_matrix(C_final,N);
  }
  #endif
  MPI_Finalize();

}