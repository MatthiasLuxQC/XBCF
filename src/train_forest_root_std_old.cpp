#include <ctime>
#include <RcppArmadillo.h>
#include "tree.h"
#include "treefuns.h"
#include "forest.h"
#include <chrono>

using namespace std;

using namespace chrono;

// [[Rcpp::plugins(cpp11)]]
// [[Rcpp::export]]
Rcpp::List train_forest_root_std_old(arma::mat y, arma::mat X, arma::mat Xtest, size_t M, size_t K, size_t L, size_t N_sweeps, arma::mat max_depth, size_t Nmin, size_t Ncutpoints, double alpha, double beta, double tau, size_t mtry = 0, bool draw_sigma = false, double kap = 16, double s = 4, bool verbose = false, bool m_update_sigma = false, bool draw_mu = false, bool parallel = true)
{
    auto start = system_clock::now();


    size_t num_trees = M + K;


    size_t N = X.n_rows;
    size_t p = X.n_cols;
    size_t N_test = Xtest.n_rows;

    assert(mtry <= p);

    if (mtry == 0)
    {
        mtry = p;
    }

    if (mtry != p)
    {
        cout << "Sample " << mtry << " out of " << p << " variables when grow each tree." << endl;
    }

    arma::umat Xorder(X.n_rows, X.n_cols);
    for (size_t i = 0; i < X.n_cols; i++)
    {
        Xorder.col(i) = arma::sort_index(X.col(i));
    }

    std::default_random_engine(generator);

    ///////////////////////////////////////////////////////////////////
    // inefficient! Need replacement
    std::vector<double> y_std(N);
    double y_mean = 0.0;
    for (size_t i = 0; i < N; i++)
    {
        y_std[i] = y(i, 0);
        y_mean = y_mean + y_std[i];
    }
    y_mean = y_mean / (double)N;

    Rcpp::NumericMatrix X_std(N, p);
    for (size_t i = 0; i < N; i++)
    {
        for (size_t j = 0; j < p; j++)
        {
            X_std(i, j) = X(i, j);
        }
    }

    Rcpp::NumericMatrix Xtest_std(N_test, p);
    for (size_t i = 0; i < N_test; i++)
    {
        for (size_t j = 0; j < p; j++)
        {
            Xtest_std(i, j) = Xtest(i, j);
        }
    }

    xinfo_sizet Xorder_std;
    ini_xinfo_sizet(Xorder_std, N, p);
    for (size_t i = 0; i < N; i++)
    {
        for (size_t j = 0; j < p; j++)
        {
            Xorder_std[j][i] = Xorder(i, j);
        }
    }

    ///////////////////////////////////////////////////////////////////

    double *ypointer = &y_std[0];
    double *Xpointer = &X_std[0];
    double *Xtestpointer = &Xtest_std[0];

    xinfo yhats_std;
    ini_xinfo(yhats_std, N, N_sweeps);
    xinfo yhats_test_std;
    ini_xinfo(yhats_test_std, N_test, N_sweeps);

    // save predictions of each tree
    std::vector<std::vector<double>> predictions_std;
    ini_xinfo(predictions_std, N, num_trees);

    xinfo predictions_test_std;
    ini_xinfo(predictions_test_std, N_test, num_trees);

    std::vector<double> yhat_std(N);
    row_sum(predictions_std, yhat_std);
    std::vector<double> yhat_test_std(N_test);
    row_sum(predictions_test_std, yhat_test_std);

    // current residual
    std::vector<double> residual_std(N);

    xinfo sigma_draw_std;
    ini_xinfo(sigma_draw_std, num_trees, N_sweeps);

    forest trees_std(num_trees);

    std::vector<double> reshat_std;
    std::vector<double> reshat_test_std;

    ///////////////////////////////////////////////////////////////////

    Rcpp::NumericMatrix yhats(N, N_sweeps);
    Rcpp::NumericMatrix yhats_test(N_test, N_sweeps);

    // save predictions of each tree
    Rcpp::NumericMatrix sigma_draw(num_trees, N_sweeps);

    double sigma;
    // double tau;
    forest trees(num_trees);
    std::vector<double> prob(2, 0.5);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::discrete_distribution<> d(prob.begin(), prob.end());
    // // sample one index of split point
    size_t prune;

    // std::vector<double> split_var_count(p);
    // std::fill(split_var_count.begin(), split_var_count.end(), 1);
    Rcpp::NumericVector split_var_count(p, 1);

    // this count is fixed at 1, always uniform sampling
    Rcpp::NumericVector split_var_count_uniform(p, 1);

    double *split_var_count_pointer = &split_var_count[0];

    std::vector<size_t> subset_vars(mtry);
    std::iota(subset_vars.begin() + 1, subset_vars.end(), 1);

    Rcpp::IntegerVector var_index_candidate(p);
    for (size_t i = 0; i < p; i++)
    {
        var_index_candidate[i] = i;
    }

    subset_vars = Rcpp::as<std::vector<size_t>>(sample(var_index_candidate, mtry, false, split_var_count));
    // cout << subset_vars << endl;

    // size_t count = 0;

    double run_time = 0.0;

    // save tree objects to strings
    // std::stringstream treess;
    // treess.precision(10);
    // treess << L << " " << num_trees << " " << p << endl;

    // L, number of samples
    // num_trees, number of trees

    for (size_t mc = 0; mc < L; mc++)
    {

        // initialize predcitions and predictions_test
        for (size_t ii = 0; ii < num_trees; ii++)
        {
            std::fill(predictions_std[ii].begin(), predictions_std[ii].end(), y_mean / (double)num_trees);
            std::fill(predictions_test_std[ii].begin(), predictions_test_std[ii].end(), y_mean / (double)num_trees);
        }

        row_sum(predictions_std, yhat_std);
        row_sum(predictions_test_std, yhat_test_std);

        residual_std = y_std - yhat_std;

        for (size_t sweeps = 0; sweeps < N_sweeps; sweeps++)
        {

            if (verbose == true)
            {
                cout << "--------------------------------" << endl;
                cout << "number of sweeps " << sweeps << endl;
                cout << "--------------------------------" << endl;
            }

            for (size_t tree_ind = 0; tree_ind < num_trees; tree_ind++)
            {

                // if update sigma based on residual of all m trees
                if (m_update_sigma == true)
                {

                    std::gamma_distribution<double> gamma_samp((N + kap) / 2.0, 2.0 / (sum_squared(residual_std) + s));

                    sigma = 1.0 / sqrt(gamma_samp(generator));

                    sigma_draw(tree_ind, sweeps) = sigma;
                }

                // save sigma
                sigma_draw(tree_ind, sweeps) = sigma;

                // add prediction of current tree back to residual
                // then it's m - 1 trees residual

                residual_std = residual_std + predictions_std[tree_ind];

                // do the samething for residual_theta_noise, residual of m - 1 trees

                yhat_std = yhat_std - predictions_std[tree_ind];

                yhat_test_std = yhat_test_std - predictions_test_std[tree_ind];

                if (mtry != p)
                {   
                    if(tree_ind < K){
                        subset_vars = Rcpp::as<std::vector<size_t>>(sample(var_index_candidate, mtry, false, split_var_count_uniform));

                    }else{
                        subset_vars = Rcpp::as<std::vector<size_t>>(sample(var_index_candidate, mtry, false, split_var_count));
                    }
                }

                trees.t[tree_ind].grow_tree_adaptive_std(sum_vec(residual_std) / (double)N, 0, max_depth(tree_ind, sweeps), Nmin, Ncutpoints, tau, sigma, alpha, beta, draw_sigma, draw_mu, parallel, residual_std, Xorder_std, Xpointer, split_var_count_pointer, mtry, subset_vars, run_time);

                if (verbose == true)
                {
                    cout << "tree " << tree_ind << " size is " << trees.t[tree_ind].treesize() << endl;
                }

                // update prediction of current tree
                fit_new_std(trees.t[tree_ind], Xpointer, N, p, predictions_std[tree_ind]);

                // update prediction of current tree, test set
                fit_new_std(trees.t[tree_ind], Xtestpointer, N_test, p, predictions_test_std[tree_ind]);

                // update sigma based on residual of m - 1 trees, residual_theta_noise
                if (m_update_sigma == false)
                {

                    std::gamma_distribution<double> gamma_samp((N + kap) / 2.0, 2.0 / (sum_squared(residual_std) + s));

                    sigma = 1.0 / sqrt(gamma_samp(generator));

                    sigma_draw(tree_ind, sweeps) = sigma;
                }

                // update residual, now it's residual of m trees

                residual_std = residual_std - predictions_std[tree_ind];

                yhat_std = yhat_std + predictions_std[tree_ind];
                yhat_test_std = yhat_test_std + predictions_test_std[tree_ind];

                // treess << trees.t[tree_ind];
            }

            // save predictions to output matrix

            for (size_t kk = 0; kk < N; kk++)
            {
                yhats(kk, sweeps) = yhat_std[kk];
            }
            for (size_t kk = 0; kk < N_test; kk++)
            {
                yhats_test(kk, sweeps) = yhat_test_std[kk];
            }
        }
    }

    auto end = system_clock::now();

    auto duration = duration_cast<microseconds>(end - start);

    cout << "Total running time " << double(duration.count()) * microseconds::period::num / microseconds::period::den << endl;

    cout << "Running time of split Xorder " << run_time << endl;

    cout << "Count of splits for each variable " << split_var_count << endl;

    // return Rcpp::List::create(Rcpp::Named("yhats") = yhats, Rcpp::Named("yhats_test") = yhats_test, Rcpp::Named("sigma") = sigma_draw, Rcpp::Named("trees") = Rcpp::CharacterVector(treess.str()));
    return Rcpp::List::create(Rcpp::Named("yhats") = yhats, Rcpp::Named("yhats_test") = yhats_test, Rcpp::Named("sigma") = sigma_draw);
}