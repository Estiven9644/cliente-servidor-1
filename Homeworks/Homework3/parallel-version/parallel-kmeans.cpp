#include <fstream>
#include <iostream>
#include <sstream>
#include <cassert>
#include <random>
#include "load.hh"

using namespace std;
using dmat = Matrix<double>;
using ulmat = Matrix<ulist>;
using cmat = Matrix<cont>;

uint avail_films = 17770+1; 		// movies amount
uint avail_users = 2649429+1;   // users amount
uint avail_centroids = 100;	      // centroids amount
uint rate_change = 1;

void get_cent_norm(const dmat& centroids,vector<double>& cent_norm){
	/* it will calculate all centroids norm */
	cent_norm.resize(centroids.numRows());
	double dchunk = (double)centroids.numRows()/4;
	uint chunk = ceil(dchunk);

	#pragma omp parallel shared(centroids,cent_norm,chunk) num_threads(4)
	{

		#pragma omp for schedule(dynamic,chunk) nowait
		for(uint cent_id=0; cent_id < centroids.numRows(); cent_id++){
			double value = 0.0;
			for(uint movie_id=1; movie_id <= centroids.numCols(); movie_id++)
				value += pow(centroids.at(cent_id,movie_id),2);
			cent_norm[cent_id] = sqrt(value);
		}
	}

}

void get_users_norm(const cmat& dataset,vector<double>& users_norm){
	/* it will calculate all users norm */
	users_norm.resize(dataset.numRows());
	double dchunk = (double)dataset.numRows()/4;
	uint chunk = ceil(dchunk);

	const vector<cont>& users = dataset.get_cont();
	#pragma omp parallel shared(dataset,users_norm,users,chunk) num_threads(4)
	{
		#pragma omp for schedule(dynamic,chunk) nowait
		for(uint user_id=0; user_id < dataset.numRows(); user_id++) {
			double value = 0.0;
			for(auto& movie : users[user_id])
				 value += (double)pow(movie.second,2);
			users_norm[user_id] = sqrt(value);
		}
	}

}

double cent_simil(const dmat& old_centroids,dmat& new_centroids){
	/* it will calculate cosine similarity between old_cent and new_cent */
	vector<double> results;
	results.resize(old_centroids.numRows());
	double dchunk = (double)old_centroids.numRows()/4;
	uint chunk = ceil(dchunk);

	#pragma omp parallel shared(old_centroids,new_centroids,results,chunk) \
												num_threads(4)
	{
		#pragma omp for schedule(dynamic,chunk) nowait
		for(uint cent_id = 0; cent_id < old_centroids.numRows(); cent_id++){
			double Ai_x_Bi = 0.0, val1 = 0.0, val2 = 0.0;
			for(uint movie_id = 1; movie_id <= old_centroids.numCols(); movie_id++ ){
				double old_cent_rate = old_centroids.at(cent_id,movie_id);
				double new_cent_rate = new_centroids.at(cent_id,movie_id);
				val1 += pow(old_cent_rate,2);
				val2 += pow(new_cent_rate,2);
				Ai_x_Bi += old_cent_rate * new_cent_rate;
			}

			results[cent_id] += acos(Ai_x_Bi/(sqrt(val1) * sqrt(val2)) );
		}
	}

	double similarity_value = 0.0;
	for(auto& data : results)
		similarity_value += data;

	double similarity = similarity_value / old_centroids.numRows();
	return similarity;
}

void cos_simil(const cmat& dataset,const dmat& centroids,dmat& new_centroids, \
							ulmat& similarity,vector<double>& users_norm,vector<double>& \
							cent_norm){
	/* This will calculate the cosain similarity between centroids and users */
	const vector<cont>& users = dataset.get_cont();
	dmat users_rate(avail_centroids,avail_films);
	double dchunk = (double)dataset.numRows()/4;
	uint chunk = ceil(dchunk);

	omp_lock_t writelock;
	omp_init_lock(&writelock);
	#pragma omp parallel shared(dataset,centroids,similarity,cent_norm,users_norm\
		,users,chunk) num_threads(4)
	{

		#pragma omp for schedule(dynamic,chunk) nowait
		for(uint user_id=0; user_id < dataset.numRows(); user_id++){
			uint temp_cent_id = 0;
			double temp_simil_val = numeric_limits<double>::max();

			for(uint cent_id = 0; cent_id < centroids.numRows(); cent_id++){
				double Ai_x_Bi = 0.0;

				for(auto& movie : users[user_id]) {
					double cent_rate = centroids.at(cent_id,movie.first);
					double user_rate = movie.second;
					Ai_x_Bi += cent_rate * user_rate;
				}

				double similarity_value = acos( Ai_x_Bi/(cent_norm[cent_id] * \
																	users_norm[user_id]) );
				if(similarity_value < temp_simil_val){
					temp_simil_val = similarity_value;
					temp_cent_id = cent_id;
				}
			}

			/* ------ users rate summary by parts ------ */
			omp_set_lock(&writelock);
			for(auto& movie : users[user_id]){
				double& movie_rate = new_centroids.at(temp_cent_id,movie.first);
				double& users =  users_rate.at(temp_cent_id,movie.first);
				movie_rate += movie.second;
				users+=1;
			}

			similarity.fill_like_list(temp_cent_id,user_id);
			omp_unset_lock(&writelock);
		}

	}
	omp_destroy_lock(&writelock);

	dchunk = (double)centroids.numRows()/4;
	chunk = ceil(dchunk);
	#pragma omp parallel shared(centroids,new_centroids,users_rate,chunk) \
												num_threads(4)
	{
		/* ------ averaging users rate ------ */
		#pragma omp for schedule(dynamic,chunk) nowait
		for(uint cent_id=0; cent_id< centroids.numRows(); cent_id++ ){
			for(uint movie_id=1; movie_id<=centroids.numCols(); movie_id++){
				double& movie_rate = new_centroids.at(cent_id,movie_id);
				double& users = users_rate.at(cent_id,movie_id);
				if(!users) continue;
				movie_rate /= users;
			}
		}
	}

}

// void generate_cent(dmat& centroids,vector<double>& cent_norm,uint cent_id){
// 	/* it will generate a new centroid values */
// 	random_device rd;
// 	mt19937 gen(rd());
// 	uniform_real_distribution<> dis(1.0, 5.0);
// 	double norm = 0.0;
// 	for (uint movie_id = 0; movie_id < centroids.numCols(); movie_id++) {
// 		double& rate = centroids.at(cent_id,movie_id);
// 		rate = dis(gen);
// 		norm += pow(rate,2);
// 	}
// 	cent_norm[cent_id] = sqrt(norm);
//
// }

void modify_cent(uint current_cent_id,dmat& centroids, vector<double>& \
								cent_norm,const ulmat& similarity){
	/* it will modify a given centroid slightly */
	uint upper_cent_id = 0, upper_cent_size = numeric_limits<double>::min();
	const vector<ulist>& users_set = similarity.get_cont();

	for(uint cent_id=0; cent_id< similarity.numRows(); cent_id++) {
		size_t set_size = users_set[cent_id].size();
		if(set_size > upper_cent_size) {
			upper_cent_size = set_size;
			upper_cent_id = cent_id;
		}
	}

	uint val = 0.0;
	for(uint movie_id=0; movie_id < centroids.numCols(); movie_id++){
		srand(time(0));
		uint let_change = rand()%(11);
		if(let_change){
			double movie_rate = centroids.at(upper_cent_id,movie_id);
			double last_rate = movie_rate;
			movie_rate += rate_change;
			if(movie_rate > 5.0){
				movie_rate = last_rate;
				movie_rate -= rate_change;
			}
			double& origin_movie_rate = centroids.at(current_cent_id,movie_id);
			origin_movie_rate = movie_rate;
			val += pow(movie_rate,2);
		}
		else{
			double movie_rate = centroids.at(upper_cent_id,movie_id);
			double& origin_movie_rate = centroids.at(current_cent_id,movie_id);
			origin_movie_rate = movie_rate;
			val += pow(movie_rate,2);
		}
	}
	cent_norm[current_cent_id] = sqrt(val);
	cout << cent_norm[current_cent_id] << endl;

}

void check_and_repl_cent(const cmat& dataset,dmat& centroids,vector<double>& \
											cent_norm,const ulmat& similarity){
	/* it will check if exist an empty centroid, then raplaced it with an user */
	for(uint cent_id=0; cent_id < centroids.numRows() ; cent_id++) {
		if(!cent_norm[cent_id]) {

			/* --- option I take an existing user like new centroid --- */
			//srand(time(0));
			//uint new_cent_id = rand(0,dataset.numRows()+1);
			// uint randUser = rand()%((dataset.numRows() + 1) + 1);
			// cout << "rand user num = " << randUser  << '\n';
			// const vector<cont>& users = dataset.get_cont();
			// double norm = 0.0;
			// for(auto& movie : users[randUser]) {
			// 	double& movie_rate = centroids.at(cent_id,movie.first);
			// 	double rate =  movie.second;
			// cent_norm[cent_id] = sqrt(norm);

			/* --- option II generate the new centroid --- */
			// generate_cent(centroids,cent_norm,cent_id);

			/* --- option III take an existing centroid and modify it slightly --- */
			modify_cent(cent_id,centroids,cent_norm,similarity);

		}
	}

}

void print_result(const ulmat& similarity){
	/* it will print centroids with theirs nearest users */
	for(uint cent_id=0; cent_id < similarity.numRows(); cent_id++)
		cout << cent_id << " : " << similarity.get_set_size(cent_id) << "\n";
}

int main(int argc, char *argv[]){
	if(argc != 2){
		cerr << "Usage {" << argv[0] << " filename.txt}\n";
		return -1;
	}
	/* ----------- phase 1 loading info into memory ----------- */
	Matrix <cont>dataset;
	load_data(argv[1],avail_users,dataset);
	vector<double> users_norm, cent_norm;
	get_users_norm(dataset,users_norm);
	// dataset.print_dic();

	/* ----------- phase 2 building initial centroids ----------- */
	Matrix <double>centroids(avail_centroids, avail_films);
	centroids.fill_like_num();
	get_cent_norm(centroids,cent_norm);
	//centroids.print_num();

	Timer timer;
	while(true){
		/* ----------- phase 3 building similarity sets ----------- */
		Matrix <ulist>similarity(avail_centroids);
		Matrix <double>new_centroids(avail_centroids,avail_films);
		cos_simil(dataset,centroids,new_centroids,similarity,users_norm,cent_norm);
		//similarity.print_list();

		/* ----------- phase 4 cosine similraty between two centroids ----------- */
		get_cent_norm(new_centroids,cent_norm);
		check_and_repl_cent(dataset,new_centroids,cent_norm,similarity);
		double similarity_val = cent_simil(centroids,new_centroids);
		cout << "Current similarity = " << similarity_val << "\n";
		if(similarity_val < 0.1){
			print_result(similarity);
			break;
		}
		centroids = move(new_centroids);
	}
	cout << "Transcurred seconds = " <<	(double)timer.elapsed()/1000 << endl;

	return 0;
}
