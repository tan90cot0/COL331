#include <bits/stdc++.h>
#include <vector>
#include <iostream>
#include <string>

using namespace std;

#pragma GCC optimize("Ofast,unroll-loops")


#define ar array
#define ll long long
#define ld long double
#define sza(x) ((int)x.size())
#define all(a) (a).begin(), (a).end()

#define PI 3.1415926535897932384626433832795l
const int MAX_N = 1e5 + 5;
const ll MOD = 1e9 + 7;
const ll INF = 1e9;
const ld EPS = 1e-9;


vector<string> inputSV(){
    int numelems;
    cin >> numelems;
    vector<string> inputWords(numelems);
    for (int j = 0; j < numelems; j++){cin >> inputWords[j];}
    return inputWords;
}


vector<int> inputIV(){
    int numelems;
    cin >> numelems;
    vector<int> inputInts(numelems);
    for (int j = 0; j < numelems; j++){cin >> inputInts[j];}
    return inputInts;
}
int main() {
    ios_base::sync_with_stdio(0);
    cin.tie(0); cout.tie(0);
    int tc = 65;
    
    vector<double> out(tc);
    for (int t = 1; t < tc; t++) {
        //copy from inputIV if n and vector to be taken in
        double e = pow(2, 1.0/((double)t));
        double f = (double)t * (e-1);
        
        //solve here

        out[t] = f*1000000;
    }
    for(int i = 0; i < tc; i++){cout << out[i] << "\n";}
        return 0;
}
