var Album = Backbone.Model.extend(
        {
            defaults: {
                name: ''
            },
            //url: '/api/album'
            url: function() {
                return this.isNew() ? '/api/album' : ('/api/album/' + this.id);
            }
        }
        );

var AlbumCollection = Backbone.Collection.extend(
        {
            model: Album,
            comparator: 'name',
            url: '/api/album'
        }
        );

var AlbumFormView = StaticView.extend(
        {
            className: 'album-form',
            template: '<h1>Album</h1>' +
                '<form class="aligned-form">' +
                '<label for="name">Name</label><input type="text" name="name" value="<%-name%>"></input><br>' +
                '<button type="submit" class="hidden"></button>' +
                '</form>',
            events: {
                'click button': function() { this.save(); this.trigger('finished'); }
            },
            save: function() {
                var col = this.model.collection;
                this.model.save(
                    {
                        name: this.$('[name=name]').val()
                    },
                    {
                        success: function() {
                            if(col)
                                col.fetch();
                        },
                        wait: true
                    }
                    );
            }
        }
        );

var AlbumView = StaticView.extend(
        {
            tagName: 'li',
            className: 'album',
            template: '<span class="album-name"><%-name%></span>',
            events: {
                click: function() { this.trigger('click'); }
            }
        }
        );

var Photograph = Backbone.Model.extend(
        {
            defaults: {
                title: '',
                location: '',
                taken: ''
            },
            url: function() {
                return this.isNew() ? '/api/photograph' : ('/api/photograph/' + this.id);
            }
        }
        );

var PhotographCollection = Backbone.Collection.extend(
        {
            model: Photograph,
            comparator: 'taken'
            // No URL attribute is defined because photograph collections can
            // be rerieved from various URLs.
        }
        );

var PhotographFormView = StaticView.extend(
        {
            className: 'photograph-form',
            template: '<h1>Photograph</h1>' +
                '<form class="aligned-form">' +
                '<label for="title">Address</label><input type="text" name="title" value="<%=title%>"></input><br>' +
                '<label for="taken">Date</label><input type="text" name="Date" value="<%-taken%>"></input><br>' +
                '<label for="location">Location</label><input type="text" name="location" value="<%-location%>"></input><br>' +
                // TODO tags, albums
                '<button type="submit" class="hidden"></button>' +
                '</form>',
            events: {
                'click button': function() { this.save(); this.trigger('finished'); }
            },
            save: function() {
                console.log('save',{
                    title: this.$('[name=title]').val(),
                    taken: this.$('[name=taken]').val(),
                    location: this.$('[name=location]').val()
                });
                this.model.save(
                    {
                        title: this.$('[name=title]').val(),
                        taken: this.$('[name=taken]').val(),
                        location: this.$('[name=location]').val()
                    },
                    {
                        success: function() {
                            if(col)
                                col.fetch();
                        },
                        wait: true
                    }
                    );
            }
        }
        );

var ThumbnailView = StaticView.extend(
        {
            tagName: 'li',
            className: 'thumbnail',
            template: '<a href="/photograph.html#<%-id%>"><img src="/photograph/small/<%-id%>" alt="<%-title%>"></img><span class="vertical-align-helper"></span></a>',
            events: {
                click: function() {
                    this.trigger('click')
                }
            }
        }
        );

var PhotographView = StaticView.extend(
        {
            template: '<img src="/photograph/medium/<%-id%>" alt="<%-title%>"></img><br><button name="edit">Edit</button>',
            events: {
                'click button[name=edit]': function() {
                    (new Modal({
                        view: PhotographFormView,
                        model: this.model,
                        buttons: [
                            StandardButton.cancel(),
                            StandardButton.destroy(
                                function() {
                                    new ConfirmModal(
                                            {
                                                message: 'Are you sure you want to delete this photograph?',
                                                callback: (function() {
                                                    this.model.destroy();
                                                    this.remove();
                                                }).bind(this)
                                            }
                                            );
                                }
                                ),
                            StandardButton.save(
                                function() {
                                    this.view.save();
                                    this.remove();
                                }
                                )
                        ]
                    }));
                }
            }
        }
        );

