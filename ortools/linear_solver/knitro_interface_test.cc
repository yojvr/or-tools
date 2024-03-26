// Copyright 2010-2024 Google LLC
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <filesystem>
#include <fstream>
#include <locale>
#include <vector>
#include <stdio.h>

#include "gtest/gtest.h"
#include "ortools/base/init_google.h"
#include "ortools/linear_solver/linear_solver.h"
#include "ortools/knitro/environment.h"

namespace operations_research {

#define EXPECT_STATUS(s)                              \
  do {                                                \
    int const status_ = s;                            \
    EXPECT_EQ(0, status_) << "Nonzero return status"; \
  } while (0)

class KnitroGetter {
 public:
  KnitroGetter(MPSolver* solver) : solver_(solver) {}

  // Var getters
  void Num_Var(int *NV){
    EXPECT_STATUS(KN_get_number_vars(kc(), NV));
  }

  void Var_Lb(MPVariable* x, double *lb){
    CHECK(solver_->OwnsVariable(x));
    EXPECT_STATUS(KN_get_var_lobnd(kc(), x->index(), lb));
  }

  void Var_Ub(MPVariable* x, double *ub){
    CHECK(solver_->OwnsVariable(x));
    EXPECT_STATUS(KN_get_var_upbnd(kc(), x->index(), ub));
  }

  void Var_Name(MPVariable* x, char *name, int buffersize){
    CHECK(solver_->OwnsVariable(x));
    EXPECT_STATUS(KN_get_var_name(kc(), x->index(), buffersize, name));
  }

  // Cons getters
  void Num_Cons(int *NC){
    EXPECT_STATUS(KN_get_number_cons(kc(), NC));
  }

  void Con_Lb(MPConstraint *ct, double *lb){
    EXPECT_STATUS(KN_get_con_lobnd(kc(), ct->index(), lb));
  }

  void Con_Ub(MPConstraint *ct, double *ub){
    EXPECT_STATUS(KN_get_con_upbnd(kc(), ct->index(), ub));
  }

  void Con_Name(MPConstraint* ct, char *name, int buffersize){
    EXPECT_STATUS(KN_get_con_name(kc(), ct->index(), buffersize, name));
  }

  // Obj getters
  // TODO

 private:
  MPSolver* solver_;
  KN_context_ptr kc() {return (KN_context_ptr)solver_->underlying_solver();}

};

#define UNITTEST_INIT_MIP()                                                  \
  MPSolver solver("KNITRO_MIP", MPSolver::KNITRO_MIXED_INTEGER_PROGRAMMING); \
  KnitroGetter getter(&solver)
#define UNITTEST_INIT_LP()                                           \
  MPSolver solver("KNITRO_LP", MPSolver::KNITRO_LINEAR_PROGRAMMING); \
  KnitroGetter getter(&solver)

TEST(Env, CheckEnv){
  EXPECT_EQ(KnitroIsCorrectlyInstalled(),true);
}

TEST(KnitroInterface, SetAndWriteModel){
  // max  x + 2y
  // st. 3x - 4y >= 10
  //     2x + 3y <= 18
  //      x,   y \in R+

  UNITTEST_INIT_LP();
  const double infinity = solver.solver_infinity();

  MPVariable* const x = solver.MakeNumVar(0.0, infinity, "x");
  MPVariable* const y = solver.MakeNumVar(0.0, infinity, "y");

  MPConstraint* const c1 = solver.MakeRowConstraint(10.0, infinity, "c1");
  c1->SetCoefficient(x, 3);
  c1->SetCoefficient(y, -4);
  MPConstraint* const c2 = solver.MakeRowConstraint(-infinity, 18.0, "c2");
  c2->SetCoefficient(x, 2);
  c2->SetCoefficient(y, 3);

  MPObjective* const obj  = solver.MutableObjective();
  obj->SetCoefficient(x,1);
  obj->SetCoefficient(y,2);
  obj->SetMaximization();

  solver.Write("LP_model.param");

  // Check variable x
  double lb,ub;
  char name[20];
  getter.Var_Lb(x, &lb);
  getter.Var_Ub(x, &ub);
  getter.Var_Name(x,name, 20);
  EXPECT_EQ(lb, 0.0);
  EXPECT_EQ(ub, infinity);
  EXPECT_STREQ(name, "x");

  // Check constraint c1
  getter.Con_Lb(c1, &lb);
  getter.Con_Ub(c1, &ub);
  getter.Con_Name(c1,name, 20);
  EXPECT_EQ(lb, 10.0);
  EXPECT_EQ(ub, infinity);
  EXPECT_STREQ(name, "c1");
}

TEST(KnitroInterface, CheckWrittenModel){
  // Read model using Knitro and solve it
  // max  x + 2y
  // st. 3x - 4y >= 10
  //     2x + 3y <= 18
  //      x,   y \in R+

  KN_context *kc;
  KN_new(&kc);
  KN_load_mps_file(kc, "LP_model.param");
  KN_set_int_param(kc, KN_PARAM_OUTLEV, KN_OUTLEV_NONE);
  // Check variables
  double lb[2], ub[2];
  KN_get_var_lobnds_all(kc, lb);
  KN_get_var_upbnds_all(kc, ub);
  EXPECT_EQ(lb[0], 0);
  EXPECT_EQ(lb[1], 0);
  EXPECT_EQ(ub[0], KN_INFINITY);
  EXPECT_EQ(ub[1], KN_INFINITY);
  char *names[2];
  names[0] = new char[20];
  names[1] = new char[20];
  KN_get_var_names_all(kc, 20, names);
  EXPECT_STREQ(names[0], "x");
  EXPECT_STREQ(names[1], "y");
  // Check constraints
  KN_get_con_lobnds_all(kc, lb);
  KN_get_con_upbnds_all(kc, ub);
  EXPECT_EQ(lb[0], 10);
  EXPECT_EQ(lb[1], -KN_INFINITY);
  EXPECT_EQ(ub[0], KN_INFINITY);
  EXPECT_EQ(ub[1], 18);
  KN_get_con_names_all(kc, 20, names);
  EXPECT_STREQ(names[0], "c1");
  EXPECT_STREQ(names[1], "c2");
  // Check everything else by solving the lp
  double objSol;
  int nStatus = KN_solve (kc);
  double x[2];
  KN_get_solution(kc, &nStatus, &objSol, x, NULL);
  EXPECT_NEAR(x[0],6, 1e-6);
  EXPECT_NEAR(x[1],2, 1e-6);
  EXPECT_NEAR(objSol, 10, 1e-6);

  delete[] names[0];
  delete[] names[1];
  KN_free(&kc);
}

TEST(KnitroInterface, SolveLP) {
  auto solver = MPSolver::CreateSolver("KNITRO_LP");
  // max   x + 2y
  // st.  -x +  y <= 1
  //      2x + 3y <= 12
  //      3x + 2y <= 12
  //       x ,  y \in R+

  double inf = solver->infinity();
  MPVariable* x = solver->MakeNumVar(0, inf, "x");
  MPVariable* y = solver->MakeNumVar(0, inf, "y");
  MPObjective* obj = solver->MutableObjective();
  obj->SetCoefficient(x, 1);
  obj->SetCoefficient(y, 2);
  obj->SetMaximization();
  MPConstraint* c1 = solver->MakeRowConstraint(-inf, 1);
  c1->SetCoefficient(x, -1);
  c1->SetCoefficient(y, 1);
  MPConstraint* c2 = solver->MakeRowConstraint(-inf, 12);
  c2->SetCoefficient(x, 3);
  c2->SetCoefficient(y, 2);
  MPConstraint* c3 = solver->MakeRowConstraint(-inf, 12);
  c3->SetCoefficient(x, 2);
  c3->SetCoefficient(y, 3);
  solver->Solve();

  // check feastol ...
  EXPECT_NEAR(obj->Value(), 7.4, 1e-6);
  EXPECT_NEAR(x->solution_value(), 1.8, 1e-6);
  EXPECT_NEAR(y->solution_value(), 2.8, 1e-6);
  // EXPECT_NEAR(x->reduced_cost(), 0, 1e-6);
  // EXPECT_NEAR(y->reduced_cost(), 0, 1e-6);
  // EXPECT_NEAR(c1->dual_value(), 0.2, 1e-6);
  // EXPECT_NEAR(c2->dual_value(), 0, 1e-6);
  // EXPECT_NEAR(c3->dual_value(), 0.6, 1e-6);
}

TEST(KnitroInterface, SolveMIP){
  // max  x -  y + 5z
  // st.  x + 2y -  z <= 19.5
  //      x +  y +  z >= 3.14
  //      x           <= 10
  //           y +  z <= 6
  //      x,   y,   z \in R+

  auto solver = MPSolver::CreateSolver("KNITRO");
  const double infinity = solver->infinity();
  // x and y are integer non-negative variables.
  MPVariable* const x = solver->MakeNumVar(0.0, infinity, "x");
  MPVariable* const y = solver->MakeNumVar(0.0, infinity, "y");
  MPVariable* const z = solver->MakeIntVar(0.0, infinity, "z");

  // x + 2 * y - z <= 19.5
  MPConstraint* const c0 = solver->MakeRowConstraint(-infinity, 19.5, "c0");
  c0->SetCoefficient(x, 1);
  c0->SetCoefficient(y, 2);
  c0->SetCoefficient(z, -1);

  // x + y + z >= 3.14
  MPConstraint* const c1 = solver->MakeRowConstraint(3.14, infinity, "c1");
  c1->SetCoefficient(x, 1);
  c1->SetCoefficient(y, 1);
  c1->SetCoefficient(z, 1);

  // x <= 10.
  MPConstraint* const c2 = solver->MakeRowConstraint(-infinity, 10.0, "c2");
  c2->SetCoefficient(x, 1);
  c2->SetCoefficient(y, 0);
  c2->SetCoefficient(z, 0);

  // y + z <= 6.
  MPConstraint* const c3 = solver->MakeRowConstraint(-infinity, 6.0, "c3");
  c3->SetCoefficient(x, 0);
  c3->SetCoefficient(y, 1);
  c3->SetCoefficient(z, 1);

  // Maximize x - y + 5 * z.
  MPObjective* const objective = solver->MutableObjective();
  objective->SetCoefficient(x, 1);
  objective->SetCoefficient(y, -1);
  objective->SetCoefficient(z, 5);
  objective->SetMaximization();

  const MPSolver::ResultStatus result_status = solver->Solve();
  EXPECT_NEAR(objective->Value(), 40, 1e-7);
  EXPECT_NEAR(x->solution_value(), 10, 1e-7);
  EXPECT_NEAR(y->solution_value(), 0, 1e-7);
  EXPECT_NEAR(z->solution_value(), 6, 1e-7);
}

TEST(KnitroInterface, JustVar){
  // max x + y + z
  // st. x,  y,  z >= 0
  //     x,  y,  z <= 1
  UNITTEST_INIT_LP();
  std::vector<MPVariable*> x;
  solver.MakeNumVarArray(3, 0, 1, "x", &x);
  MPObjective *const obj = solver.MutableObjective();
  obj->SetCoefficient(x[0], 1);
  obj->SetCoefficient(x[1], 1);
  obj->SetCoefficient(x[2], 1);
  obj->SetMaximization();

  solver.Solve();
  EXPECT_NEAR(obj->Value(), 3, 1e-6);
}

TEST(KnitroInterface, FindFeasSol){
  // find a 3x3 non trivial magic square config
  UNITTEST_INIT_MIP();
  double infinity = solver.infinity();
  std::vector<MPVariable*> x;
  solver.MakeIntVarArray(9,1,infinity,"x", &x);

  std::vector<MPVariable*> diff;
  solver.MakeBoolVarArray(36, "diff", &diff);

  int debut[] = {0,8,15,21,26,30,33,35};
  for (int i=0; i<9; i++){
    for (int j=i+1; j<9; j++){
        MPConstraint* const d = solver.MakeRowConstraint(1.0, 8.0, "dl"+10*i+j);
        d->SetCoefficient(x[i], 1);
        d->SetCoefficient(x[j], -1); 
        d->SetCoefficient(diff[debut[i]+j-1-i], 9.0);
    }
  }

  int ref[] = {0,1,2};
  int comp[][3] = {
    {3,4,5},
    {6,7,8},
    {0,3,6},
    {7,1,4},
    {5,8,2},
    {0,4,8},
    {4,6,2}
  };

  for (auto e : comp){
    MPConstraint* const d = solver.MakeRowConstraint(0,0,"eq");
    for (int i=0;i<3;i++){
      if (ref[i]!=e[i]){
        d->SetCoefficient(x[ref[i]],1);
        d->SetCoefficient(x[e[i]],-1);
      }
    }
  }

  solver.Solve();
  for (int i=0; i<9; i++){
    for (int j=i+1; j<9; j++){
      EXPECT_NE(x[i]->solution_value(), x[j]->solution_value());
    }
  }
  int val = x[ref[0]]->solution_value() + x[ref[1]]->solution_value() + x[ref[2]]->solution_value();
  for (auto e : comp){
    int comp_val = x[e[0]]->solution_value() + x[e[1]]->solution_value() + x[e[2]]->solution_value();
    EXPECT_EQ(val, comp_val);
  }
}

TEST(KnitroInterface, ChangePostsolve){
  // max   x
  // st.   x +  y >= 2
  //     -2x +  y <= 4
  //       x +  y <= 10
  //       x -  y <= 8
  //       x ,  y >= 0

  UNITTEST_INIT_LP();
  const double infinity = solver.solver_infinity();

  MPVariable* const x = solver.MakeNumVar(0.0, infinity, "x");
  MPVariable* const y = solver.MakeNumVar(0.0, infinity, "y");

  MPConstraint* const c1 = solver.MakeRowConstraint(2, infinity, "c1");
  c1->SetCoefficient(x, 1);
  c1->SetCoefficient(y, 1);
  MPConstraint* const c2 = solver.MakeRowConstraint(-infinity, 4, "c2");
  c2->SetCoefficient(x, -2);
  c2->SetCoefficient(y, 1);
  MPConstraint* const c3 = solver.MakeRowConstraint(-infinity, 10, "c3");
  c3->SetCoefficient(x, 1);
  c3->SetCoefficient(y, 1);
  MPConstraint* const c4 = solver.MakeRowConstraint(-infinity, 8, "c4");
  c4->SetCoefficient(x, 1);
  c4->SetCoefficient(y, -1);

  MPObjective* const obj  = solver.MutableObjective();
  obj->SetCoefficient(x,1);
  obj->SetMaximization();

  solver.Solve();
  EXPECT_NEAR(obj->Value(), 9, 1e-7);

  obj->SetCoefficient(x,0);
  obj->SetCoefficient(y,1);

  solver.Solve();
  EXPECT_NEAR(obj->Value(), 8, 1e-7);

  y->SetBounds(2,4);
  solver.Solve();
  EXPECT_NEAR(obj->Value(), 4, 1e-7);

  y->SetBounds(0,infinity);
  obj->SetCoefficient(x,1);
  obj->SetCoefficient(y,0);
  c4->SetBounds(2,6);
  solver.Solve();
  EXPECT_NEAR(obj->Value(), 8, 1e-7);
}

TEST(KnitroInterface, ChangeVarIntoInteger){
  // max   x  
  // st.   x + y >= 2.5
  //       x + y >= -2.5
  //       x - y <= 2.5
  //       x - y >= -2.5
  //       x , y \in R
  UNITTEST_INIT_MIP();
  double infinity = solver.solver_infinity();
  MPVariable* const x = solver.MakeNumVar(-infinity, infinity, "x");
  MPVariable* const y = solver.MakeNumVar(-infinity, infinity, "y");
  MPConstraint* const c1 = solver.MakeRowConstraint(-infinity, 2.5, "c1");
  c1->SetCoefficient(x,1);
  c1->SetCoefficient(y,1);
  MPConstraint* const c2 = solver.MakeRowConstraint(-2.5, infinity, "c2");
  c2->SetCoefficient(x,1);
  c2->SetCoefficient(y,1);
  MPConstraint* const c3 = solver.MakeRowConstraint(-infinity, 2.5, "c3");
  c3->SetCoefficient(x,1);
  c3->SetCoefficient(y,-1);
  MPConstraint* const c4 = solver.MakeRowConstraint(-2.5, infinity, "c4");
  c4->SetCoefficient(x,1);
  c4->SetCoefficient(y,-1);

  MPObjective* const obj  = solver.MutableObjective();
  obj->SetCoefficient(x,1);
  obj->SetMaximization();

  solver.Solve();
  EXPECT_NEAR(obj->Value(), 2.5, 1e-7);

  x->SetInteger(true);
  solver.Solve();
  EXPECT_NEAR(obj->Value(), 2, 1e-7);
}

TEST(KnitroInterface, AddVarAndConstraint){
  // max x + y                max x + y + z
  // st. x , y <= 1;    ->    st. x , y , z >= 0
  //     x , y >= 0;              x , y , z <= 1
  UNITTEST_INIT_MIP();
  double infinity = solver.solver_infinity();
  MPVariable* const x = solver.MakeNumVar(0, 1, "x");
  MPVariable* const y = solver.MakeNumVar(0, 1, "y");

  MPObjective* const obj  = solver.MutableObjective();
  obj->SetCoefficient(x,1);
  obj->SetCoefficient(y,1);
  obj->SetMaximization();

  solver.Solve();
  EXPECT_NEAR(obj->Value(), 2, 1e-7);

  MPVariable* const z = solver.MakeNumVar(0,infinity, "z");
  MPConstraint* const c = solver.MakeRowConstraint(0, 1, "c");
  c->SetCoefficient(z, 1);
  obj->SetCoefficient(z,1);

  solver.Solve();   
  EXPECT_NEAR(obj->Value(), 3, 1e-7);
}

}  // namespace operations_research

int main(int argc, char** argv) {
  absl::SetFlag(&FLAGS_logtostderr, 1);
  testing::InitGoogleTest(&argc, argv);
  if (!RUN_ALL_TESTS()){
    remove("LP_model.param");
    return EXIT_SUCCESS;
  }
  return EXIT_FAILURE;
}
