int sub(int a1, int a2)
{
  *(int *)(a1) = a2;
  return a1;
}
