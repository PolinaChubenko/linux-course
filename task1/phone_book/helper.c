#include <stdio.h>
#include <string.h>
#include <errno.h>

#define FILE_NAME "/dev/phone_book"
#define MAX_FIELD_SIZE 32
#define MAX_AGE_SIZE 3

struct user_data {
    char name[MAX_FIELD_SIZE];
    char surname[MAX_FIELD_SIZE];
    char age[MAX_AGE_SIZE];
    char number[MAX_FIELD_SIZE];
    char email[MAX_FIELD_SIZE];
};

long add_user(struct user_data* user) {
    FILE *fp;
    if ((fp = fopen(FILE_NAME, "w+")) == NULL) {
        perror("Unable to open file\n");
        return -1;
    }
    fprintf(fp, "+ %s %s %s %s %s",
            user->name, user->surname, user->age, user->number, user->email);
    fclose(fp);

    char result[MAX_FIELD_SIZE];
    if ((fp = fopen(FILE_NAME, "r")) == NULL) {
        perror("Unable to open file\n");
        return -1;
    }
    fscanf(fp, "%s", result);
    fclose(fp);

    char* success = "Added successfully\n\0";
    return strcmp(result, success) == 0;
}

long get_user(struct user_data* user, char* surname) {
    FILE *fp;
    if ((fp = fopen(FILE_NAME, "w+")) == NULL) {
        perror("Unable to open file\n");
    }
    fprintf(fp, "? %s\n", surname);
    fclose(fp);

    if ((fp = fopen(FILE_NAME, "r")) == NULL) {
        perror("Unable to open file\n");
    }
    int status = fscanf(fp, "Name: %s\nSurname: %s\nAge: %s\nPhone number: %s\nEmail: %s\n",
           user->name, user->surname, user->age, user->number, user->email);
    fclose(fp);

    if (status != 5) {
        user->name[0] = '\0';
        return -1;
    }
    return 0;
}

long del_user(char* surname) {
    FILE *fp;
    if ((fp = fopen(FILE_NAME, "w+")) == NULL) {
        perror("Unable to open file\n");
    }
    fprintf(fp, "- %s", surname);
    fclose(fp);

    char result[MAX_FIELD_SIZE];
    if ((fp = fopen(FILE_NAME, "r")) == NULL) {
        perror("Unable to open file\n");
    }
    fscanf(fp, "%s", result);
    fclose(fp);

    char* success = "Removed successfully\n\0";
    return strcmp(result, success) == 0;
}



long is_the_same(struct user_data* u1, struct user_data* u2) {
    return strcmp(u1->name, u2->name) == 0 && strcmp(u1->surname, u2->surname) == 0 &&
        strcmp(u1->age, u2->age) == 0 && strcmp(u1->number, u2->number) == 0 && strcmp(u1->email, u2->email) == 0;
}


int main(void) {
    // example of usage
    struct user_data main_user = {
            "Polina",
            "Chubenko",
            "20",
            "89991234567",
            "penguiners"
    };
    struct user_data temp_user;

    if (get_user(&temp_user, "Chubenko") != -1) {
        perror("Empty phone book works incorrectly\n");
        return -1;
    }
    printf("%s\n", "Check of empty phone book succeeded");


    if (add_user(&main_user) == -1) {
        perror("Phone book failed to add user\n");
        return -1;
    }
    printf("%s\n", "Check of adding to phone book succeeded");


    if (get_user(&temp_user, "Chubenko") == -1) {
        perror("Phone book works incorrectly\n");
        return -1;
    }
    if (!is_the_same(&main_user, &temp_user)) {
        perror("Phone book returned incorrect user in get query\n");
        return -1;
    }
    printf("%s\n", "Check of getting user from phone book succeeded");


    if (del_user("Chubenko") == -1) {
        perror("Phone book failed to delete user\n");
        return -1;
    }
    if (get_user(&temp_user, "Chubenko") != -1) {
        perror("Phone book delete works incorrectly\n");
        return -1;
    }
    printf("%s\n", "Check of deleting user from phone book succeeded");


    printf("%s\n", "Works correctly");
    return 0;
}
